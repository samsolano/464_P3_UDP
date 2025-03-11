#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>

#include "gethostbyname.h"
#include "networks.h"
#include "safeUtil.h"
#include "libcpe464/checksum.h"
#include "pollLib.h"
#include "window.h"
#include "cpe464.h"

#define SETUP 0
#define USE 1
#define TEARDOWN 2

void processClients(int socketNumber);
int checkArgs(int argc, char *argv[]);
void createPDU(uint8_t *dataPacket, uint8_t *packetPayload, uint16_t payloadLen, uint8_t flag);
void childProcess(struct sockaddr * client, int file, int windowSize, int bufferSize);
void createChild(int socketNum, struct sockaddr *client, int file, int windowSize, int bufferSize);
void resendPacket(int sequenceNumber, int socketNum, struct sockaddr *client, int bufferSize);
void sendDataPacket(int socketNum, struct sockaddr *client, int file, int bufferSize);
void processPacket(int socketNum, struct sockaddr *client, int bufferSize);
void sendWithRetries(int socketNum, void * buf, int len, struct sockaddr *srcAddr, int addrLen);
int recvAndCheck(int socketNum, void * buf, int len, struct sockaddr *srcAddr, int * addrLen);
void teardown(int socketNum, struct sockaddr * client, int bufferSize);

int seqNum = 0;
float error = 0.0;

int main( int argc, char *argv[]  )
{ 
	int socketNum = 0;				
	int portNumber = 0;

	portNumber = checkArgs(argc, argv);

	sendtoErr_init(error, DROP_ON, FLIP_ON, DEBUG_ON, RSEED_ON);
		
	socketNum = udpServerSetup(portNumber);

	processClients(socketNum);

	close(socketNum);
	
	return 0;
}

void processClients(int socketNum) {
	setupPollSet();
	addToPollSet(socketNum);

	struct sockaddr_in6 client;
	int clientAddrLen = sizeof(client);	
	uint8_t packet[MAXBUF];
	uint8_t recvPacket[MAXBUF];
	int file;

	int windowSize = 0;
	int16_t bufferSize = 0;

	while (1) {

		pollCall(-1);																										// block till connection

		int dataLen = 0;
		if ((dataLen = recvAndCheck(socketNum, packet, MAXBUF, (struct sockaddr *) &client, &clientAddrLen)) < 0) {			//receive the fn packet and check checksum

			continue;
		}

		uint8_t fileName[packet[13]];
		memset(fileName, 0, packet[13]);

		memcpy(&windowSize, packet + 7, 4);
		memcpy(&bufferSize, packet + 11, 2);
		memcpy(fileName, packet + 14, packet[13]);
		fileName[packet[13]] = 0;																						// store values from packet

		file = open((const char *) fileName, O_RDONLY);
		if (file == -1) {
			perror("Error opening file");
			uint8_t packet[7];
			createPDU(packet, NULL, 0, 34);
			safeSendto(socketNum, packet, 7, 0, (struct sockaddr *) &client, sizeof(client));									// send error packet if fail
			continue;
		}
		else {
			
			int flagReceived = 0;

			createPDU(packet, NULL, 0, 9);																					// create success packet

			while (flagReceived != 33) {

				sendWithRetries(socketNum, packet, 7, (struct sockaddr *) &client, sizeof(client));								// send success packet

				while (recvAndCheck(socketNum, recvPacket, 7, (struct sockaddr *) &client, &clientAddrLen) < 0) {				// make sure successful received ack	
				
					sendWithRetries(socketNum, packet, 7, (struct sockaddr *) &client, sizeof(client));							// if bad checksum resend packet
				}
				
				flagReceived = recvPacket[6];																					// if successful checksum and packet has flag 33

			}

			createChild(socketNum,(struct sockaddr *) &client, file, windowSize, bufferSize);
		}
	}
}

void createChild(int socketNum, struct sockaddr * client, int file, int windowSize, int bufferSize) {

	pid_t pid = fork();

	if(pid < 0) {

		perror("Fork Failed");
	}
	else if(pid == 0) {															// create child
																				
		close(socketNum);
		childProcess(client, file, windowSize, bufferSize);
	}	
}

void childProcess(struct sockaddr * client, int file, int windowSize, int bufferSize) {

	sendtoErr_init(error, DROP_ON, FLIP_ON, DEBUG_ON, RSEED_ON);
	int socketNum = socket(AF_INET6, SOCK_DGRAM, 0);
	setupPollSet();
	addToPollSet(socketNum);

	setupWindow(windowSize);

	while (1) {
		
		while(windowOpen()) {

			sendDataPacket(socketNum, client, file, bufferSize);									// send data packet

			if (pollCall(0) > 0) {																	// if there is a packet received process it

				processPacket(socketNum, client, bufferSize);
			}	
		}

		uint8_t failNumber = 0;																		// to keep track of number of times nothing received
		while(!windowOpen()) {

			int polledSocket = pollCall(1000);														// poll for a second

			if (polledSocket > 0) {																	// if receive something process the packet
				processPacket(socketNum, client, bufferSize);
				failNumber = 0;
			}
			else if (failNumber == 10) {															// if fail 10 times exit program
				perror("connection failed, 10 failed attempts");
				exit(-1);
			}
			else {																					// if nothing received and not at 10 fails, send lowest packet in window
				resendPacket(getLower(), socketNum, client, bufferSize);
				failNumber++;
			}
		}
	}
}

void sendDataPacket(int socketNum, struct sockaddr * client, int file, int bufferSize) {

	uint8_t packetPayload[bufferSize];
	uint8_t dataPacket[bufferSize + 7];
	int packetType = 16;

	int bytes = read(file, packetPayload, bufferSize);												// fill payload with bytes from file

	if (bytes == 0) { exit(0); }
	else if(bytes == -1) { perror("Error reading file"); exit(-1); }
	else if (bytes < bufferSize) {
		//handle last packet
		packetType = 10;
	}
	
	createPDU(dataPacket, packetPayload, bytes, packetType);												// fill packet with header and payload
	addToWindow((char *) dataPacket, bytes + 7, seqNum);										// add packet to payload
	safeSendto(socketNum, dataPacket, bytes + 7, 0, client, sizeof(struct sockaddr_in6)); 
	seqNum++;

	if (packetType == 10) {
		teardown(socketNum, client, bufferSize);
	}
}

void teardown(int socketNum, struct sockaddr * client, int bufferSize) {

	int socketReturned = 0;
	int fails = 0;

	while(1) {

		socketReturned = pollCall(1000);


		if (socketReturned > 0) {
			processPacket(socketNum, client, bufferSize);
		}
		else if (fails == 10) {
			perror("Failed 10 times teardown\n");
		}
		else {
			resendPacket(getLower(), socketNum, client, bufferSize);
			fails++;
		}
	}
}

void processPacket(int socketNum, struct sockaddr *client, int bufferSize) {

	uint8_t buffer[11];
	int clientAddrLen = sizeof(client);	
	int responseNumberNetwork = 0;
	int responseNumberHost = 0;


	if (recvAndCheck(socketNum, buffer, 11, client, &clientAddrLen) < 0) {

		return;
	}	

	int flag = buffer[6];
	
	memcpy(&responseNumberNetwork, buffer + 7, 4);
	responseNumberHost = ntohl(responseNumberNetwork);

	if (flag == 5) {		// RR

		setLower(responseNumberHost + 1);
	}
	else if (flag == 6) {		// SREJ

		resendPacket(responseNumberHost, socketNum, client, bufferSize);
	}
	else if (flag == 35) {

		exit(0);
	}
}


void resendPacket(int sequenceNumber, int socketNum, struct sockaddr *client, int bufferSize) {

	uint8_t dataPacket[bufferSize + 7];
	memcpy(dataPacket, getWindowEntry(sequenceNumber), getEntryLen(sequenceNumber));
	safeSendto(socketNum, dataPacket, getEntryLen(sequenceNumber), 0, client, sizeof(struct sockaddr_in6));
}


void sendWithRetries(int socketNum, void * buf, int len, struct sockaddr *srcAddr, int addrLen) {

	int numOfTries = 0;

	while (numOfTries < 10) {																			// try max of 10 times

		safeSendto(socketNum, buf, (size_t) len, 0, srcAddr, addrLen);
		numOfTries++;
		if (pollCall(1000) > 0) {																		// if socket is ready to receive then set variable to exit while 
			numOfTries = 69;
		}
	}

	if (numOfTries == 69) {
		return;
	}

	perror("Packet resent 10 times with no response: program ending");
	exit(-1);
}

int recvAndCheck(int socketNum, void * buf, int len, struct sockaddr *srcAddr, int * addrLen) {

	int returnValue = safeRecvfrom(socketNum, buf, len, 0, srcAddr, addrLen);
	unsigned short checksum = in_cksum(buf, returnValue);

	if (checksum != 0) {
		
		return -1;
	}

	return returnValue;
}

void createPDU(uint8_t *dataPacket, uint8_t *packetPayload, uint16_t payloadLen, uint8_t flag) {

	memset(dataPacket, 0, payloadLen + 7);
	int networkSequence = htonl(seqNum);
	uint8_t flagInput = flag;

	memcpy(dataPacket, &networkSequence, 4);
	dataPacket[4] = 0;
	dataPacket[5] = 0;
	memcpy(dataPacket + 6, &flagInput, 1);

	if (payloadLen > 0) { memcpy(dataPacket + 7, packetPayload, payloadLen); }

	unsigned short checksum = in_cksum((unsigned short *)dataPacket, 7 + payloadLen);
	memcpy(dataPacket + 4, &checksum, 2);
}

int checkArgs(int argc, char *argv[])
{
	// Checks args and returns port number
	int portNumber = 0;
	error = atof(argv[1]);

	if (argc > 3 | error < 0.0 | error > 1.0) {

		fprintf(stderr, "Usage %s error-rate (0.0 - 1.0) [optional port number]\n", argv[0]);
		exit(-1);
	}
	
	if (argc == 3) {

		portNumber = atoi(argv[2]);
	}

	return portNumber;
}
