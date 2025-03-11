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
#include "pollLib.h"
#include "window.h"

#define INORDER 0
#define BUFFERING 1
#define FLUSHING 2
#define RR 5
#define SREJ 6


void establishConnection(int socketNum, struct sockaddr_in6 * server);
int readFromStdin(char * buffer);
int checkArgs(int argc, char * argv[]);
void createPDUSERVERFIX(uint8_t *dataPacket, uint8_t *packetPayload, uint16_t payloadLen, uint8_t flag);
void clientUse(int socketNum, struct sockaddr_in6 * server, int toFileDescriptor, int packetZeroBytes);
void sendWithRetries(int socketNum, void * buf, int len, struct sockaddr *srcAddr, int addrLen);
int recvAndCheck(int socketNum, void * buf, int len, struct sockaddr *srcAddr, int * addrLen);
void sendRRorSREJ(int value, int flag, struct sockaddr *srcAddr, int addrLen, int socketNum);
int checkFile();
void teardown(int socketNum, struct sockaddr_in6 * server, int serverAddrLen);


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
	struct sockaddr_in6 server;
	int portNumber = 0;
	
	portNumber = checkArgs(argc, argv);
	
	socketNum = setupUdpClientToServer(&server, argv[6], portNumber);

	sendtoErr_init(errorRate, DROP_ON, FLIP_ON, DEBUG_ON, RSEED_ON);
	
	establishConnection(socketNum, &server);

	close(socketNum);

	return 0;
}


void establishConnection(int socketNum, struct sockaddr_in6 * server) {

	int toFileDescriptor = checkFile();

	setupPollSet();																			// create poll set and put main socket in
	addToPollSet(socketNum);

	uint8_t recvPacket[bufferSize + 7];
	uint8_t payload[bufferSize];
	memset(payload, 0, bufferSize);
	
	int fileLen = strlen((const char *) requestedFile);										// take terminal args and put them into packet
	uint8_t sendPacket[fileLen + 14];

	memcpy(payload, &windowLen, 4);
	memcpy(payload + 4, &bufferSize, 2);
	memcpy(payload + 6, &fileLen, 1);
	memcpy(payload + 7, requestedFile, payload[6]);


	createPDUSERVERFIX(sendPacket, payload, 7 + fileLen, 8);								// add app pdu header to packet, 8 is initialize flag

	int flagReceived = 0;
	int bytes_received = 0;
	int serverAddrLen = sizeof(struct sockaddr_in6);
 
	while (flagReceived != 9 && flagReceived != 34) {																			// if different flags received retry whole process

		sendWithRetries(socketNum, sendPacket, 14 + fileLen, (struct sockaddr *) server, sizeof(struct sockaddr_in6)); 			// send inital fn packet

		while (recvAndCheck(socketNum, recvPacket, 7, (struct sockaddr *) server, &serverAddrLen) < 0) {						//receive fn ack from server and verify checksum
			
			sendWithRetries(socketNum, sendPacket, 14 + fileLen, (struct sockaddr *) server, sizeof(struct sockaddr_in6));	// if checksum fails, keep sending back packets
		}
		flagReceived = recvPacket[6];

		if (flagReceived == 9)	{																								// successful
			
			createPDUSERVERFIX(sendPacket, NULL, 0, 33);		
				
			while (flagReceived != 16 && flagReceived != 10) {																						// check that flag now is a normal data packet

				sendWithRetries(socketNum, sendPacket, 7, (struct sockaddr *) server, serverAddrLen);		// send final ack to server

				while ((bytes_received = recvAndCheck(socketNum, recvPacket, bufferSize + 7, (struct sockaddr *) server, &serverAddrLen)) < 0) {						
				
					sendWithRetries(socketNum, sendPacket, 7, (struct sockaddr *) server, sizeof(struct sockaddr_in6));
				}

				flagReceived = recvPacket[6];
			}

			setupWindow(windowLen);																								//when data is received setup window and store data packet
			addToWindow((char *) recvPacket, bytes_received, seqNum++);																	//move now to use portion
			clientUse(socketNum, server, toFileDescriptor, bytes_received); 
			return;
			
		}
		else if (flagReceived == 34) {																								//file not found

			fprintf(stderr, "Error: file %s not found\n", requestedFile);
			exit(-1);
		}
	}
}

void clientUse(int socketNum, struct sockaddr_in6 * server, int toFileDescriptor, int packetZeroBytes) {

	int serverAddrLen = sizeof(struct sockaddr_in6);
	uint8_t recvPacket[bufferSize + 7];
	uint8_t state = INORDER;
	int sequenceNumberNetwork = 0;
	int sequenceNumberHost = 0;
	int expected = 0;
	int highest = 0;
	int bytes_received = 0;

	// ----------------------------------this takes care of 0th data packet received--------------------------------------------------------------------
	uint8_t recvPacket0[bufferSize + 7];
	memcpy(recvPacket0, getWindowEntry(0), bufferSize + 7);
	memcpy(&sequenceNumberNetwork, recvPacket0, 4);
	
	if(sequenceNumberNetwork == expected) {

		write(toFileDescriptor, recvPacket0 + 7, packetZeroBytes - 7);
		highest = expected;
		expected++;
		sendRRorSREJ(highest, RR, (struct sockaddr *) server, serverAddrLen, socketNum);

		if(recvPacket0[6] == 10) {

			teardown(socketNum, server, serverAddrLen);
		}
	}
	else if (sequenceNumberNetwork > expected) {

		state = BUFFERING;
		sendRRorSREJ(expected, SREJ, (struct sockaddr *) server, serverAddrLen, socketNum);
		addToWindow((char *) recvPacket, bytes_received, sequenceNumberHost);
		if(sequenceNumberHost > highest) {
			highest = sequenceNumberHost;
		}
	}
	// -------------------------------------------------------------------------------------------------------------------------------------------------


	while (1) {

		if(pollCall(10000) > 0) {

			if ((bytes_received = recvAndCheck(socketNum, recvPacket, bufferSize + 7, (struct sockaddr *) server, &serverAddrLen)) < 0) {		// if bad checksum ignore packet
				continue;
			}

			memcpy(&sequenceNumberNetwork, recvPacket, 4);
			sequenceNumberHost = ntohl(sequenceNumberNetwork);

			if(sequenceNumberHost < expected) {

				sendRRorSREJ(expected - 1, RR, (struct sockaddr *) server, serverAddrLen, socketNum);
				continue;
			}
			
			if (state == INORDER) {

				if(sequenceNumberHost == expected) {

					write(toFileDescriptor, recvPacket + 7, bytes_received - 7);
					highest = expected;
					expected++;
					sendRRorSREJ(highest, RR, (struct sockaddr *) server, serverAddrLen, socketNum);
					if(recvPacket[6] == 10) {

						teardown(socketNum, server, serverAddrLen);
					}
				}
				else if (sequenceNumberHost > expected){

					state = BUFFERING;
					sendRRorSREJ(expected, SREJ, (struct sockaddr *) server, serverAddrLen, socketNum);
					addToWindow((char *) recvPacket, bytes_received, sequenceNumberHost);
					if(sequenceNumberHost > highest) {
						highest = sequenceNumberHost;
					}
				}
			}
			else if (state == BUFFERING) {

				if (sequenceNumberHost > expected) {

					addToWindow((char *) recvPacket, bytes_received, sequenceNumberHost);
					if(sequenceNumberHost > highest) {
						highest = sequenceNumberHost;
					}
				}
				else if (sequenceNumberHost == expected) {

					write(toFileDescriptor, recvPacket + 7, bytes_received - 7);
					sendRRorSREJ(expected, RR, (struct sockaddr *) server, serverAddrLen, socketNum);
					expected++;
					state = FLUSHING;

					if(recvPacket[6] == 10) {
						teardown(socketNum, server, serverAddrLen);
						
					}
				}
			}
			if (state == FLUSHING) {

				int needToBuffer = 0;
				while (expected <= highest) {

					if(getEntryValid(expected)) {

						int lengthOfExpectedPacket = getEntryLen(expected);
						uint8_t bufferedPacket[lengthOfExpectedPacket];
						memcpy(bufferedPacket, getWindowEntry(expected), lengthOfExpectedPacket);
						write(toFileDescriptor, getWindowEntry(expected) + 7, getEntryLen(expected) - 7);
						sendRRorSREJ(expected, RR, (struct sockaddr *) server, serverAddrLen, socketNum);
						expected++;

						if(bufferedPacket[6] == 10) {
							teardown(socketNum, server, serverAddrLen);
						}	
					}
					else {

						sendRRorSREJ(expected, SREJ, (struct sockaddr *) server, serverAddrLen, socketNum);
						state = BUFFERING;
						needToBuffer = 1;
						break;
					}
				}
				
				if (needToBuffer == 0) {
					state = INORDER;
				}
			}
		}
		else {
			perror("Server timeout");
			exit(-1);
		}
	}
}


void teardown(int socketNum, struct sockaddr_in6 * server, int serverAddrLen) {

	uint8_t dataPacket[7];
	createPDUSERVERFIX(dataPacket, NULL, 0, 35);
	safeSendto(socketNum, dataPacket, 7, 0, (struct sockaddr *) server, serverAddrLen);
	exit(0);
}


void sendRRorSREJ(int value, int flag, struct sockaddr *srcAddr, int addrLen, int socketNum) {

	uint8_t dataPacket[11];
	uint8_t payload[4];
	int valHost = value;
	int valNetwork = htonl(valHost);

	memcpy(payload, &valNetwork, 4);
	createPDUSERVERFIX(dataPacket, payload, 4, flag);
	safeSendto(socketNum, dataPacket, 11, 0, srcAddr, addrLen);
	if(flag == 5) {
		setLower(valHost + 1);
	}
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


void createPDUSERVERFIX(uint8_t *dataPacket, uint8_t *packetPayload, uint16_t payloadLen, uint8_t flag) {

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


int checkArgs(int argc, char * argv[])
{

	if (argc != 8)
	{
		printf("usage: rcopy from-filename: to-filename: window-size buffer-size: error-rate remote-machine: remote-port \n");
		exit(1);
	}

	int portNumber = 0;

	if (strlen(argv[1]) > 100 || strlen(argv[2]) > 100 ||  atoi(argv[3]) > 1073741824 || atoi(argv[4]) > 1400) {				// check file names, and values in bounds

		perror("Rcopy Input Parameter Error: ");
		exit(-1);
	}

	memset(requestedFile, 0, 101);
	strcpy((char *) requestedFile, argv[1]);
	strcpy((char *) toFile, argv[2]);
	windowLen = atoi(argv[3]);
	bufferSize = atoi(argv[4]);
	errorRate = atof(argv[5]);
	portNumber = atoi(argv[7]);
		
	return portNumber;
}


int checkFile() {

	int toFileDescriptor = open((const char *) toFile, O_WRONLY | O_CREAT | O_TRUNC, 00777);
	if(toFileDescriptor < 0) { perror("cannot open to-File"); exit(-1);}
	return toFileDescriptor;
}

