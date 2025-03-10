/* Server side - UDP Code	    */
/* By Hugh Smith	4/1/2017	*/

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

#define SETUP 0
#define USE 1
#define TEARDOWN 2

void processClients(int socketNumber);
int checkArgs(int argc, char *argv[]);
void createPDU(uint8_t *dataPacket, uint8_t *packetPayload, uint16_t payloadLen, uint8_t flag);
void childProcess(int socketNum, struct sockaddr * client, int file, int windowSize, int bufferSize);
void createChild(int socketNum, struct sockaddr *client, int file, int windowSize, int bufferSize);
void resendPacket(int responseNumber, int socketNum, struct sockaddr *client, int file);
void sendDataPacket(int socketNum, struct sockaddr *client, int file, int bufferSize);
void processPacket(int socketNum, struct sockaddr *client, int file);
void sendWithRetries(int socketNum, void * buf, int len, struct sockaddr *srcAddr, int addrLen);
int recvAndCheck(int socketNum, void * buf, int len, struct sockaddr *srcAddr, int * addrLen);


void windowTesting();


int seqNum = 0;

int main( int argc, char *argv[]  )
{ 
	int socketNum = 0;				
	int portNumber = 0;

	// portNumber = checkArgs(argc, argv);
		
	// socketNum = udpServerSetup(portNumber);

	// processClients(socketNum);

	// close(socketNum);

	windowTesting();
	
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


		// printf("\n");
		// for(int i = 0; i < dataLen; i++) {

		// 	printf("%x ", packet[i]);
		// }
		// printf("\n");

		uint8_t fileName[packet[13]];
		memset(fileName, 0, packet[13]);

		memcpy(&windowSize, packet + 7, 4);
		memcpy(&bufferSize, packet + 11, 2);
		memcpy(fileName, packet + 14, packet[13]);
		fileName[packet[13]] = 0;

		// printf("filename: %s, windowSize: %d, bufferSize: %d \n", fileName, windowSize, bufferSize);							// store values from packet

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
			exit(0);
		}
	}
}

void createChild(int socketNum, struct sockaddr * client, int file, int windowSize, int bufferSize) {

	// pid_t pid = fork();

	// if(pid < 0) {
	// 	perror("Fork Failed");
	// 	exit(-1);
	// }
	// else if(pid == 0) {
	// 	childProcess(socketNum);
	// }
	childProcess(socketNum, client, file, windowSize, bufferSize);
}


void childProcess(int socketNum, struct sockaddr * client, int file, int windowSize, int bufferSize) {

											//will come back when forking children
	// close(socketNum)
	// setupPollSet();
	// addToPollSet(socketNumber);

	
	uint8_t packet[bufferSize + 7];

	uint8_t state = USE;
	uint8_t failNumber = 0;

	while (1) {

		pollCall(-1);

		uint8_t packetPayload[bufferSize];
		uint8_t dataPacket[bufferSize + 7];
		

		while(windowOpen()) {

			sendDataPacket(socketNum, client, file, bufferSize);

			//check for RR 
			
		}
		while(!windowOpen()) {
			// poll and wait for one second if successful process rr
			// if timeout then send lowest packet in window

			

			int polledSocket = pollCall(1000);

			if (polledSocket > 0) {
				processPacket(socketNum, client, file);
				failNumber = 0;
			}
			else if (failNumber == 10) {
				perror("connection failed");
				exit(-1);
			}
			else {
				failNumber++;
			}


		}
	}
}

void sendDataPacket(int socketNum, struct sockaddr * client, int file, int bufferSize) {

	uint8_t packetPayload[bufferSize];
	uint8_t dataPacket[bufferSize + 7];

	int bytes = read(file, packetPayload, bufferSize);

	if (bytes == 0) { printf("no more bytes\n"); return; }
	else if(bytes == -1) { perror("Error reading file"); }
	
	memset(dataPacket, 0, sizeof(dataPacket));
	createPDU(dataPacket, packetPayload, bytes, 16);
	addToWindow((char *) dataPacket, bufferSize + 7);
	safeSendto(socketNum, dataPacket, bytes + 7, 0, client, sizeof(client)); // 0 is for flags

	// poll to see if there is a response with 0 wait time, and process rr if there is one

	if (pollCall(0) > 0) {

		processPacket(socketNum, client, file);

	}
}

void processPacket(int socketNum, struct sockaddr *client, int file) {

	uint8_t buffer[MAXBUF];
	safeRecvfrom(socketNum, buffer, MAXBUF, 0, NULL, NULL);
	int flag = buffer[6];
	int responseNumber;
	memcpy(&responseNumber, buffer + 7, 4);

	if (flag == 5) {		// RR

		setLower(responseNumber);
	}
	else if (flag == 6) {		// SREJ

		resendPacket(responseNumber, socketNum, client, file);
	}
}


void resendPacket(int responseNumber, int socketNum, struct sockaddr *client, int file) {

	return;

}



void setup(int socketNum) {

}

void sendWithRetries(int socketNum, void * buf, int len, struct sockaddr *srcAddr, int addrLen) {
	int returnValue = 0;
	int numOfTries = 0;
	int pollResult = 0;

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
	
	//check the checksum
	// unsigned short checksumValue = 0;
	// memcpy(&checksumValue, buf + 4, 2);

	// if wrong return -1

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

	if (argc > 2)
	{
		fprintf(stderr, "Usage %s [optional port number]\n", argv[0]);
		exit(-1);
	}
	
	if (argc == 2)
	{
		portNumber = atoi(argv[1]);
	}
	
	return portNumber;
}



void windowTesting() {

	setupWindow(5);

	uint8_t packet1[7];
	uint8_t packet2[7];
	uint8_t packet3[7];
	uint8_t packet4[7];
	uint8_t packet5[7];
	uint8_t packet6[7];
	uint8_t packet7[7];
	uint8_t packet8[7];
	createPDU(packet1, NULL, 0, 1);
	createPDU(packet2, NULL, 0, 2);
	createPDU(packet3, NULL, 0, 3);
	createPDU(packet4, NULL, 0, 4);
	createPDU(packet5, NULL, 0, 5);
	createPDU(packet6, NULL, 0, 6);
	createPDU(packet7, NULL, 0, 7);
	createPDU(packet8, NULL, 0, 8);

	addToWindow((char *) packet, 7);




}