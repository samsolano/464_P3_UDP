// Client side - UDP Code				    
// By Hugh Smith	4/1/2017		

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#include "gethostbyname.h"
#include "networks.h"
#include "safeUtil.h"
#include "libcpe464/checksum.h"
#include "cpe464.h"

#define MAXBUF 80

void establishConnection(int socketNum, struct sockaddr_in6 * server);
int readFromStdin(char * buffer);
int checkArgs(int argc, char * argv[]);
void createPDUSERVERFIX(uint8_t *dataPacket, uint8_t *packetPayload, uint16_t payloadLen, uint8_t flag);


void talkToServer(int socketNum, struct sockaddr_in6 * server);

uint8_t requestedFile[101];
uint8_t toFile[101];
int windowLen = 0;
int bufferSize = 0;
float errorRate = 0.0;

int seqNum = 0;

int main (int argc, char *argv[])
 {
	int socketNum = 0;				
	struct sockaddr_in6 server;		// Supports 4 and 6 but requires IPv6 struct
	int portNumber = 0;
	
	portNumber = checkArgs(argc, argv);
	
	socketNum = setupUdpClientToServer(&server, argv[6], portNumber);

	sendErr_init(0.0, 0, 0, 0, 0);
	
	establishConnection(socketNum, &server);


	close(socketNum);

	return 0;
}


void establishConnection(int socketNum, struct sockaddr_in6 * server)
{

	// windowlen - 4, buffersize - 2, file len - 1, filename up to 100

	uint8_t dataPacket[bufferSize + 7];
	uint8_t payload[bufferSize];
	memset(payload, 0, bufferSize);

	int fileLen = strlen((const char *) requestedFile);

	memcpy(payload, &windowLen, 4);
	memcpy(payload + 4, &bufferSize, 2);
	memcpy(payload + 6, &fileLen, 1);
	memcpy(payload + 7, requestedFile, payload[6]);


	createPDUSERVERFIX(dataPacket, payload, 7 + fileLen, 8);

	// printf("\n");
	// for(int i = 0; i < (14 + fileLen); i++) {

	// 	printf("%x ", dataPacket[i]);
	// }
	// printf("\n");
	

	safeSendto(socketNum, dataPacket, 14 + fileLen, 0, (struct sockaddr *) server, sizeof(struct sockaddr_in6));
}


void createPDUSERVERFIX(uint8_t *dataPacket, uint8_t *packetPayload, uint16_t payloadLen, uint8_t flag) {

	// seqnum - 4, checksum - 2, flag - 1

	memset(dataPacket, 0, MAXBUF);
	// int networkSequence = htonl(seqNum);

	memcpy(dataPacket, &seqNum, 4);
	dataPacket[4] = 0;
	dataPacket[5] = 0;
	memcpy(dataPacket + 6, &flag, 1);

	if (payloadLen > 0) { memcpy(dataPacket + 7, packetPayload, payloadLen); }

	unsigned short payloadChecksum = in_cksum((unsigned short *)packetPayload, payloadLen);
	memcpy(dataPacket + 4, &payloadChecksum, 2);
}


int checkArgs(int argc, char * argv[])
{
	// printf("%d\n", argc);

	if (argc != 8)
	{
		printf("usage: rcopy from-filename: to-filename: window-size buffer-size: error-rate remote-machine: remote-port \n");
		exit(1);
	}

	int portNumber = 0;

	strcpy((char *) requestedFile, argv[1]);
	strcpy((char *) toFile, argv[2]);
	windowLen = atoi(argv[3]);
	bufferSize = atoi(argv[4]);
	errorRate = atof(argv[5]);
	portNumber = atoi(argv[7]);
		
	return portNumber;
}


// from-filename: to-filename: window-size buffer-size: error-rate remote-machine: remote-port:


// udpClient test.txt out.txt 10 100 0.0 localhost 62765









int readFromStdin(char * buffer)
{
	char aChar = 0;
	int inputLen = 0;        
	
	// Important you don't input more characters than you have space 
	buffer[0] = '\0';
	printf("Enter data: ");
	while (inputLen < (MAXBUF - 1) && aChar != '\n')
	{
		aChar = getchar();
		if (aChar != '\n')
		{
			buffer[inputLen] = aChar;
			inputLen++;
		}
	}
	
	// Null terminate the string
	buffer[inputLen] = '\0';
	inputLen++;
	
	return inputLen;
}









