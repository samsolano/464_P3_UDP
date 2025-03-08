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
void childProcess(int socketNum, struct sockaddr_in6 client, int file, int windowSize, int bufferSize);
void createChild(int socketNum, struct sockaddr_in6 client, int file, int windowSize, int bufferSize);
void resendPacket(int responseNumber, int socketNum, struct sockaddr_in6 client, int file);
void sendDataPacket(int socketNum, struct sockaddr_in6 client, int file, int bufferSize);
void processPacket(int socketNum, struct sockaddr_in6 client, int file);

void processClient(int socketNum);


int seqNum = 0;

int main( int argc, char *argv[]  )
{ 
	int socketNum = 0;				
	int portNumber = 0;

	portNumber = checkArgs(argc, argv);
		
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
	int file;

	uint8_t windowSize[4];
	uint8_t bufferSize[2];

	while (1) {

		pollCall(-1);

		printf("received packet\n");
		//receive filename establishment
		// safeRecvfrom(socketNum, buffer, MAXBUF, 0, (struct sockaddr *) &client, &clientAddrLen);


		int dataLen = safeRecvfrom(socketNum, packet, MAXBUF, 0, (struct sockaddr *) &client, &clientAddrLen);


		printf(" received Len: %d \'%s\'\n", dataLen, packet);


		printf("\n");
		for(int i = 0; i < dataLen; i++) {

			printf("%x ", packet[i]);
		}
		printf("\n");



		uint8_t fileName[packet[13]];
		memset(fileName, 0, packet[13]);

		memcpy(windowSize, packet + 7, 4);
		memcpy(bufferSize, packet + 11, 2);
		memcpy(fileName, packet + 14, packet[13]);
		fileName[packet[13]] = '\0'

		windowSize = ntohl(*windowSize);
		bufferSize = ntohl(*bufferSize);

		printf("filename: %s, windowSize: %04x, bufferSize: %02x\n", fileName, windowSize, bufferSize);

		return;






		// file = fopen((const char *) fileName, "r");
		file = open("test.txt", O_RDONLY);
		if (file == -1) {
			perror("Error opening file");
			return;
		}
		else if ( (int)*bufferSize > 1400) {
			perror("buffer too large");
			return;
		}
		else {

			createChild(socketNum, client, file, *windowSize, *bufferSize);
		}


	}
}

void createChild(int socketNum, struct sockaddr_in6 client, int file, int windowSize, int bufferSize) {

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


void childProcess(int socketNum, struct sockaddr_in6 client, int file, int windowSize, int bufferSize) {

											//will come back when forking children
	// close(socketNum)
	// setupPollSet();
	// addToPollSet(socketNumber);

	uint8_t packet[MAXBUF];
	createPDU(packet, NULL, 0, 9);
	safeSendto(socketNum, packet, 7, 0, (struct sockaddr *) &client, sizeof(client)); //send fileName Ack in child

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

void sendDataPacket(int socketNum, struct sockaddr_in6 client, int file, int bufferSize) {

	uint8_t packetPayload[bufferSize];
	uint8_t dataPacket[bufferSize + 7];

	int bytes = read(file, packetPayload, bufferSize);

	if (bytes == 0) { printf("no more bytes\n"); return; }
	else if(bytes == -1) { perror("Error reading file"); }
	
	memset(dataPacket, 0, sizeof(dataPacket));
	createPDU(dataPacket, packetPayload, bytes, 16);
	addToWindow(dataPacket, bufferSize + 7);
	safeSendto(socketNum, dataPacket, bytes + 7, 0, (struct sockaddr *) &client, sizeof(client)); // 0 is for flags

	// poll to see if there is a response with 0 wait time, and process rr if there is one

	if (pollCall(0) > 0) {

		processPacket(socketNum, client, file);

	}
}

void processPacket(int socketNum, struct sockaddr_in6 client, int file) {

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


void resendPacket(int responseNumber, int socketNum, struct sockaddr_in6 client, int file) {

	return;

}



void setup(int socketNum) {

}





void createPDU(uint8_t *dataPacket, uint8_t *packetPayload, uint16_t payloadLen, uint8_t flag) {

	memset(dataPacket, 0, MAXBUF);
	int networkSequence = htonl(seqNum);

	memcpy(dataPacket, &networkSequence, 4);
	dataPacket[4] = 0;
	dataPacket[5] = 0;
	memcpy(dataPacket + 6, &flag, 1);

	if (payloadLen > 0) { memcpy(dataPacket + 7, packetPayload, payloadLen); }

	unsigned short payloadChecksum = in_cksum((unsigned short *)packetPayload, payloadLen);
	memcpy(dataPacket + 4, &payloadChecksum, 2);
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


// void processClient(int socketNum)
// {
// 	int dataLen = 0; 
// 	char buffer[MAXBUF -6];	  
// 	struct sockaddr_in6 client;		
// 	int clientAddrLen = sizeof(client);	

// 	printf("now waiting\n");
	
// 	buffer[0] = '\0';
// 	while (buffer[0] != '.')
// 	{
// 		dataLen = safeRecvfrom(socketNum, buffer, MAXBUF - 7, 0, (struct sockaddr *) &client, &clientAddrLen);
	
// 		printf("Received message from client with ");
// 		printIPInfo(&client);
// 		printf(" Len: %d \'%s\'\n", dataLen, buffer);

// 		// // just for fun send back to client number of bytes received
// 		// sprintf(buffer, "bytes: %d", dataLen);
// 		// safeSendto(socketNum, buffer, strlen(buffer)+1, 0, (struct sockaddr *) & client, clientAddrLen);

// 	}
// }
