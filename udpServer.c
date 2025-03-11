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
void resendPacket(int sequenceNumber, int socketNum, struct sockaddr *client, int bufferSize);
void sendDataPacket(int socketNum, struct sockaddr *client, int file, int bufferSize);
void processPacket(int socketNum, struct sockaddr *client, int bufferSize);
void sendWithRetries(int socketNum, void * buf, int len, struct sockaddr *srcAddr, int addrLen);
int recvAndCheck(int socketNum, void * buf, int len, struct sockaddr *srcAddr, int * addrLen);


void windowTesting();


int seqNum = 0;
int teardown = 0;

int main( int argc, char *argv[]  )
{ 
	int socketNum = 0;				
	int portNumber = 0;

	portNumber = checkArgs(argc, argv);
		
	socketNum = udpServerSetup(portNumber);

	processClients(socketNum);

	close(socketNum);

	// windowTesting();
	
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

	// printf("in child\n");

	setupWindow(windowSize);
	uint8_t packet[bufferSize + 7];
	

	while (1) {
		
		
		while(windowOpen()) {
			// printf("in window open\n");
			uint8_t packetPayload[bufferSize];
			uint8_t dataPacket[bufferSize + 7];

			sendDataPacket(socketNum, client, file, bufferSize);									// send data packet

			if (pollCall(0) > 0) {																	// if there is a packet received process it
				// printf("received in open\n");
				processPacket(socketNum, client, bufferSize);

			}	
		}

		uint8_t failNumber = 0;																		// to keep track of number of times nothing received

		while(!windowOpen()) {

			printf("in window closed\n");
			int polledSocket = pollCall(1000);														// poll for a second

			if (polledSocket > 0) {																	// if receive something process the packet
				processPacket(socketNum, client, bufferSize);
				failNumber = 0;
			}
			else if (failNumber == 10) {															// if fail 10 times exit program
				perror("connection failed");
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

	int bytes = read(file, packetPayload, bufferSize);												// fill payload with bytes from file

	if (bytes == 0) { printf("no more bytes\n"); exit(0); }
	else if(bytes == -1) { perror("Error reading file"); exit(-1); }
	else if (bytes < bufferSize) {
		//handle last packet
		printf("bytes < buffer, %d < %d\n", bytes, bufferSize);
	}
	
	createPDU(dataPacket, packetPayload, bytes, 16);												// fill packet with header and payload
	addToWindow((char *) dataPacket, bufferSize + 7, seqNum);										// add packet to payload
	

	safeSendto(socketNum, dataPacket, bytes + 7, 0, client, sizeof(struct sockaddr_in6)); 
	printf("%d: packet sent\n", seqNum);
	seqNum++;
}

void processPacket(int socketNum, struct sockaddr *client, int bufferSize) {

	uint8_t buffer[11];
	int clientAddrLen = sizeof(client);	
	int responseNumberNetwork = 0;
	int responseNumberHost = 0;


	if (recvAndCheck(socketNum, buffer, 11, client, &clientAddrLen) < 0) {

		printf("received bad packet\n");
		return;
	}	

	int flag = buffer[6];
	
	memcpy(&responseNumberNetwork, buffer + 7, 4);
	responseNumberHost = ntohl(responseNumberNetwork);

	if (flag == 5) {		// RR
		printf("\tRR packet for %d\n", responseNumberHost);
		setLower(responseNumberHost + 1);
	}
	else if (flag == 6) {		// SREJ
		printf("\tSREJ packet\n");
		resendPacket(responseNumberHost, socketNum, client, bufferSize);
	}
}


void resendPacket(int sequenceNumber, int socketNum, struct sockaddr *client, int bufferSize) {

	printf("Resending Packet %d\n", sequenceNumber);
	uint8_t dataPacket[bufferSize + 7];
	memcpy(dataPacket, getWindowEntry(sequenceNumber), bufferSize + 7);
	safeSendto(socketNum, dataPacket, bufferSize + 7, 0, client, sizeof(struct sockaddr_in6));
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



	uint8_t packet0[7];
	uint8_t packet1[7];
	uint8_t packet2[7];
	uint8_t packet3[7];
	uint8_t packet4[7];
	uint8_t packet5[7];
	uint8_t packet6[7];
	uint8_t packet7[7];
	uint8_t packet8[7];

	createPDU(packet0, NULL, 0, 0);
	createPDU(packet1, NULL, 0, 1);
	createPDU(packet2, NULL, 0, 2);
	createPDU(packet3, NULL, 0, 3);
	createPDU(packet4, NULL, 0, 4);
	createPDU(packet5, NULL, 0, 5);
	createPDU(packet6, NULL, 0, 6);
	createPDU(packet7, NULL, 0, 7);
	createPDU(packet8, NULL, 0, 8);

	
	addToWindow((char *) packet0, 7, seqNum++);
	addToWindow((char *) packet1, 7, seqNum++);
	addToWindow((char *) packet2, 7, seqNum++);
	addToWindow((char *) packet3, 7, seqNum++);
	addToWindow((char *) packet4, 7, seqNum++);

	
	
	// client recieves first two packets and says its ready for next packet

	// setCurrent(seqNum);



	// send first 5 packets							(0, 5, 5)					
	printAll();
	printWindowValues();


	// get RR for first 2 packets					(2, 5, 7)
	setLower(2);
	printWindowValues();

	printAll();
	printWindowValues();

	// get RR for next 2 packets (RR 3)				(4, 5, 9)
	setLower(4);
	printWindowValues();

	// send 3 more packets							(4, 9, 9)		
	addToWindow((char *) packet5, 7, seqNum++);
	addToWindow((char *) packet6, 7, seqNum++);
	addToWindow((char *) packet7, 7, seqNum++);
	printf("window is %d\n", windowOpen());
	addToWindow((char *) packet8, 7, seqNum++);

	printf("window is %d\n", windowOpen());

	printAll();
	printWindowValues();

	setLower(7);
	printWindowValues();

	printAll();
	printWindowValues();



// lower is lowest value in window
// current is next value to be sent
// upper is value just out of the window

}