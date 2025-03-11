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

	sendtoErr_init(errorRate, DROP_ON, FLIP_ON, DEBUG_ON, RSEED_OFF);
	
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

	// printf("\n");
	// for(int i = 0; i < (14 + fileLen); i++) {

	// 	printf("%x ", sendPacket[i]);
	// }
	// printf("\n");

	int flagReceived = 0;
	int bytes_received = 0;
	int serverAddrLen = sizeof(struct sockaddr_in6);
 
	while (flagReceived != 9 && flagReceived != 34) {																			// if different flags received retry whole process

		sendWithRetries(socketNum, sendPacket, 14 + fileLen, (struct sockaddr *) server, sizeof(struct sockaddr_in6)); 			// send inital fn packet

		while (recvAndCheck(socketNum, recvPacket, 7, (struct sockaddr *) server, &serverAddrLen) < 0) {						//receive fn ack from server and verify checksum
			
			sendWithRetries(socketNum, sendPacket, 14 + fileLen, (struct sockaddr *) server, sizeof(struct sockaddr_in6));	// if checksum fails, keep sending back packets
		}
		flagReceived = recvPacket[6];

		// printf("fn ack received, flag received should be 9: %d\n", flagReceived);

		if (flagReceived == 9)	{																								// successful
			
			createPDUSERVERFIX(sendPacket, NULL, 0, 33);		
				
			while (flagReceived != 16 && flagReceived != 10) {																						// check that flag now is a normal data packet

				sendWithRetries(socketNum, sendPacket, 7, (struct sockaddr *) server, serverAddrLen);		// send final ack to server

				while ((bytes_received = recvAndCheck(socketNum, recvPacket, bufferSize + 7, (struct sockaddr *) server, &serverAddrLen)) < 0) {						
				
					sendWithRetries(socketNum, sendPacket, 7, (struct sockaddr *) server, sizeof(struct sockaddr_in6));
				}

				flagReceived = recvPacket[6];
			}

			// printf("first data packet received\n");

			//add first data packet here and move to use portion now, also try opening to-file and give error if cant open like in server
			setupWindow(windowLen);																								//when data is received setup window and store data packet
			addToWindow((char *) recvPacket, bytes_received, seqNum++);																	//move now to use portion
			// write(toFileDescriptor, recvPacket + 7, bufferSize);
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

	// int serverAddrLen = sizeof(struct sockaddr_in6);
	// uint8_t recvPacket0[bufferSize + 7];

	// memcpy(recvPacket0, getWindowEntry(0), bufferSize + 7);

	// printf("\n");
	// 	for(int i = 0; i < bufferSize + 7; i++) {

	// 		printf("%02x ", recvPacket0[i]);
	// 	}
	// 	printf("\n");


	// while(1) {

	// 	uint8_t recvPacket[bufferSize + 7];

	// 	pollCall(-1);
	// 	recvAndCheck(socketNum, recvPacket, bufferSize + 7, (struct sockaddr *) server, &serverAddrLen);

	// 	printf("\n");
	// 	for(int i = 0; i < bufferSize + 7; i++) {

	// 		printf("%02x ", recvPacket[i]);
	// 	}
	// 	printf("\n");

	// }
	// return;



	












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
	printf("0th seqNum: %d\n", sequenceNumberNetwork);

	
	if(sequenceNumberNetwork == expected) {
		printf("writing %d, bytes: %d\n", sequenceNumberHost, packetZeroBytes - 7);
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
		printf("\treceived %d not expected %d\n",sequenceNumberHost, expected);
		sendRRorSREJ(expected, SREJ, (struct sockaddr *) server, serverAddrLen, socketNum);
		addToWindow((char *) recvPacket, bytes_received, sequenceNumberHost);
		if(sequenceNumberHost > highest) {
			highest = sequenceNumberHost;
		}
	}
	// -------------------------------------------------------------------------------------------------------------------------------------------------


	while (1) {

		if(pollCall(10000) > 0) {	// figure out whats supposed to happen here

			if ((bytes_received = recvAndCheck(socketNum, recvPacket, bufferSize + 7, (struct sockaddr *) server, &serverAddrLen)) < 0) {		// if bad checksum ignore packet
				continue;
			}

			memcpy(&sequenceNumberNetwork, recvPacket, 4);
			sequenceNumberHost = ntohl(sequenceNumberNetwork);
			printf("\n--------------------------------------------------------------------------\n");
			printf("seqNum: %d, expected: %d, highest: %d\n\n", sequenceNumberHost, expected, highest);

			if(sequenceNumberHost < expected) {

				sendRRorSREJ(expected - 1, RR, (struct sockaddr *) server, serverAddrLen, socketNum);
				continue;
			}
			

			if (state == INORDER) {

				printf("inorder\n");
				if(sequenceNumberHost == expected) {
					printf("\twriting %d\n", sequenceNumberHost);
					write(toFileDescriptor, recvPacket + 7, bytes_received - 7);
					highest = expected;
					expected++;
					printf("\ninorder rr %d packet\n", highest);
					sendRRorSREJ(highest, RR, (struct sockaddr *) server, serverAddrLen, socketNum);

					if(recvPacket[6] == 10) {
						teardown(socketNum, server, serverAddrLen);
						
					}
				}
				else if (sequenceNumberHost > expected){

					state = BUFFERING;
					printf("\treceived %d not expected %d\n",sequenceNumberHost, expected);
					sendRRorSREJ(expected, SREJ, (struct sockaddr *) server, serverAddrLen, socketNum);
					addToWindow((char *) recvPacket, bytes_received, sequenceNumberHost);
					if(sequenceNumberHost > highest) {
						highest = sequenceNumberHost;
					}


					// srej expected #
					// buffer recvPacket
					// highest = sequenceNumberHost;
				}
			}
			else if (state == BUFFERING) {

				printf("in buffering\n");
				if (sequenceNumberHost > expected) {

					printf("\tbuffering %d\n", sequenceNumberHost);
					addToWindow((char *) recvPacket, bytes_received, sequenceNumberHost);
					printAll();
					printWindowValues();

					if(sequenceNumberHost > highest) {
						highest = sequenceNumberHost;
					}

					// buffer received packet
					// highest = sequenceNumberHost
				}
				else if (sequenceNumberHost == expected) {

					printf("\twriting buffered packet %d\n", sequenceNumberHost);
					write(toFileDescriptor, recvPacket + 7, bytes_received - 7);
					
					printf("\tbuffering rr %d packet\n\n", expected);
					sendRRorSREJ(expected, RR, (struct sockaddr *) server, serverAddrLen, socketNum);
					expected++;

					state = FLUSHING;

					if(recvPacket[6] == 10) {
						teardown(socketNum, server, serverAddrLen);
						
					}
					
					//write packet
					//RR packet
					//expected++
					
				}

			}
			if (state == FLUSHING) {

				int needToBuffer = 0;

				printf("in flush\n");
				printAll();
				while (expected <= highest) {

					printf("\texpected: %d, highest: %d\n", expected, highest);
					if(getEntryValid(expected)) {

						int lengthOfExpectedPacket = getEntryLen(expected);
						uint8_t bufferedPacket[lengthOfExpectedPacket];
						memcpy(bufferedPacket, getWindowEntry(expected), lengthOfExpectedPacket);

						

						printf("\twriting %d in flush\n", expected);
						write(toFileDescriptor, getWindowEntry(expected) + 7, getEntryLen(expected) - 7);
						sendRRorSREJ(expected, RR, (struct sockaddr *) server, serverAddrLen, socketNum);
						printf("\tflushing rr %d packet\n", expected);
						expected++;


						if(bufferedPacket[6] == 10) {
							teardown(socketNum, server, serverAddrLen);
						}
						
						
					}
					else {

						printf("\t%d not in window in flush\n", expected);
						sendRRorSREJ(expected, SREJ, (struct sockaddr *) server, serverAddrLen, socketNum);
						state = BUFFERING;
						needToBuffer = 1;
						break;
					}
					
				}
				
				
				if(needToBuffer == 0) {
					state = INORDER;
				}

				// while expected <= highest
				// if expected is valid - write, rr, expected++
				// else if expected is not valid, srej expected, state = buffering
				// if expected

				// state = inorder

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
	printf("final ack sent\n");
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

	// printf("\n");
	// for(int i = 0; i < len; i++) {

	// 	printf("%x ", ((uint8_t *)buf)[i] );
	// }
	// printf("\n");
}


void createPDUSERVERFIX(uint8_t *dataPacket, uint8_t *packetPayload, uint16_t payloadLen, uint8_t flag) {

	// seqnum - 4, checksum - 2, flag - 1

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
	// printf("%d\n", argc);

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


// from-filename: to-filename: window-size buffer-size: error-rate remote-machine: remote-port:


// udpClient test.txt out.txt 10 100 0.0 localhost 62765





int checkFile() {

	int toFileDescriptor = open((const char *) toFile, O_WRONLY | O_CREAT | O_TRUNC, 00777);
	if(toFileDescriptor < 0) { perror("cannot open to-File"); exit(-1);}
	return toFileDescriptor;
}



// int readFromStdin(char * buffer)
// {
// 	char aChar = 0;
// 	int inputLen = 0;        
	
// 	// Important you don't input more characters than you have space 
// 	buffer[0] = '\0';
// 	printf("Enter data: ");
// 	while (inputLen < (MAXBUF - 1) && aChar != '\n')
// 	{
// 		aChar = getchar();
// 		if (aChar != '\n')
// 		{
// 			buffer[inputLen] = aChar;
// 			inputLen++;
// 		}
// 	}
	
// 	// Null terminate the string
// 	buffer[inputLen] = '\0';
// 	inputLen++;
	
// 	return inputLen;
// }











// #include <stdio.h>
// #include <stdlib.h>
// #include <sys/types.h>
// #include <sys/stat.h>
// #include <sys/uio.h>
// #include <sys/time.h>
// #include <unistd.h>
// #include <fcntl.h>
// #include <string.h>
// #include <strings.h>
// #include <sys/socket.h>
// #include <netinet/in.h>
// #include <netdb.h>

// #include "gethostbyname.h"
// #include "networks.h"
// #include "safeUtil.h"
// #include "window.h"
// #include "cpe464.h"
// #include "checksum.h"
// #include "pollLib.h"
// #include "helper.h"

// typedef enum {
// 	FILENAME,
// 	FILENAME_ACK_WAIT,
// 	OPEN_FILE,
// 	RECV_DATA,
// 	DONE
// } State;

// typedef enum {
// 	INORDER,
// 	BUFFERING,
// 	FLUSHING,
// 	EOF_STATE,
// 	TIMEOUT
// } BufferState;

// void sendToServer(int socketNum, struct sockaddr_in6 * server, uint8_t *send_pdu, int pdu_len);
// int recvFromServer(int socketNum, struct sockaddr_in6 * server, uint8_t * recvPDU);
// int checkArgs(int argc, char * argv[]);
// void rcopyControl(int serverSocket, char * argv[], struct sockaddr_in6 * server);
// void sendFilename(int serverSocket, char * argv[], State *current_state, struct sockaddr_in6 * server);
// void recvFilenameAck(int serverSocket, char * argv[], State *current_state, struct sockaddr_in6 * server);
// void initializeFile(State *current_state, uint8_t *filename, FILE **file);
// void recvData(int serverSocket, char * argv[], State *current_state, struct sockaddr_in6 * server, FILE *file);
// void inorder(int serverSocket, char * argv[], BufferState *buffer_state, struct sockaddr_in6 * server, FILE *file, uint32_t *expected, uint32_t *sequence_num, uint32_t *highest);
// void buffering(int serverSocket, char * argv[], BufferState *buffer_state, struct sockaddr_in6 * server, FILE *file, uint32_t *expected, uint32_t *sequence_num, uint32_t *highest);
// void flushing(int serverSocket, char * argv[], BufferState *buffer_state, struct sockaddr_in6 * server, FILE *file, uint32_t *expected, uint32_t *sequence_num, uint32_t *highest, uint8_t eof_flag);

// int main (int argc, char *argv[])
//  {
// 	int socketNum = 0;
// 	struct sockaddr_in6 server;		// Supports 4 and 6 but requires IPv6 struct
// 	int portNumber = 0;

// 	portNumber = checkArgs(argc, argv);

// 	sendtoErr_init(atof(argv[ERROR_RATE]), DROP_ON, FLIP_ON, DEBUG_ON, RSEED_OFF);

// 	socketNum = setupUdpClientToServer(&server, argv[HOST_NAME], portNumber);

// 	rcopyControl(socketNum, argv, &server);

// 	close(socketNum);

// 	return 0;
// }

// void sendToServer(int socketNum, struct sockaddr_in6 * server, uint8_t *send_pdu, int pdu_len) {
// 	int serverAddrLen = sizeof(struct sockaddr_in6);
// 	safeSendto(socketNum, send_pdu, pdu_len, 0, (struct sockaddr *) server, serverAddrLen);
// }

// int recvFromServer(int socketNum, struct sockaddr_in6 * server, uint8_t * recvPDU) {
// 	int dataLen = 0;
// 	uint8_t buffer[MAXBUF] = {0};
// 	int serverAddrLen = sizeof(server);
// 	dataLen = safeRecvfrom(socketNum, buffer, MAXBUF, 0, (struct sockaddr *) server, &serverAddrLen);
// 	memcpy(recvPDU, buffer, MAXBUF);
// 	return dataLen;
// }

// int checkArgs(int argc, char * argv[]) {
// 	int portNumber = 0;
// 	/* check command line arguments  */
// 	if (argc != 8) {
// 		printf("usage: %s from-filename to-filename window-size buffer-size error-rate remote-machine remote-port\n", argv[0]);
// 		exit(1);
// 	}
// 	if (strlen(argv[FROM_FILE]) > 100 || strlen(argv[TO_FILE]) > 100) {
// 		printf("Error: Maximum filename length is 100 characters\n");
// 		exit(1);
// 	}
// 	portNumber = atoi(argv[7]);
// 	return portNumber;
// }

// void rcopyControl(int serverSocket, char * argv[], struct sockaddr_in6 * server) {
// 	setupPollSet();
// 	addToPollSet(serverSocket);
// 	createWindow(atoi(argv[WINDOW_SIZE]));
// 	FILE *file = NULL;
// 	State current_state = FILENAME;
// 	while(1) {
// 		switch(current_state) {
// 			case FILENAME:
// 				sendFilename(serverSocket, argv, &current_state, server);
// 				break;
// 			case FILENAME_ACK_WAIT:
// 				recvFilenameAck(serverSocket, argv, &current_state, server);
// 				break;
// 			case OPEN_FILE:
// 				initializeFile(&current_state, (uint8_t *)argv[TO_FILE], &file);
// 				break;
// 			case RECV_DATA:
// 				recvData(serverSocket, argv, &current_state, server, file);
// 				break;
// 			case DONE:
// 				fclose(file);
// 				close(serverSocket);
// 				exit(0);
// 				break;
// 		}
// 	}
// }

// void sendFilename(int serverSocket, char * argv[], State *current_state, struct sockaddr_in6 * server) {
// 	uint8_t payload[MAXBUF] = {0};
// 	*(uint32_t *)payload = htonl((uint32_t)atoi(argv[WINDOW_SIZE]));
// 	*(uint16_t *)(payload + 4) = htons((uint16_t)atoi(argv[BUFFER_SIZE]));
// 	payload[6] = strlen(argv[FROM_FILE]);
// 	memcpy(payload + 7, (uint8_t *)argv[FROM_FILE], strlen(argv[FROM_FILE]));
// 	uint8_t *send_pdu = createPDU(0, EST_FLAG, payload, 7 + strlen(argv[FROM_FILE]));
// 	addPDU(send_pdu, 0, PDU_HEADER_LEN + 7 + strlen(argv[FROM_FILE]));
// 	sendToServer(serverSocket, server, send_pdu, PDU_HEADER_LEN + 7 + strlen(argv[FROM_FILE]));
// 	*current_state = FILENAME_ACK_WAIT;
// }

// uint8_t * createPDU(uint32_t sequenceNumber, uint8_t flag, uint8_t *payload, int datalen) {
// 	static uint8_t pduBuffer[MAXBUF];
// 	uint32_t seqNum = htonl(sequenceNumber);
// 	memcpy(pduBuffer, &seqNum, 4);
// 	pduBuffer[4] = 0;
// 	pduBuffer[5] = 0;
// 	pduBuffer[6] = flag;
// 	memcpy(pduBuffer + 7, payload, datalen);
// 	unsigned short checkSum = in_cksum((unsigned short *)pduBuffer, 7 + datalen); // NETWORK ORDER?
// 	memcpy(pduBuffer + 4, &checkSum, sizeof(unsigned short));
// 	pduBuffer[7 + datalen] = '\0';
// 	return pduBuffer;
//   }

// void recvFilenameAck(int serverSocket, char * argv[], State *current_state, struct sockaddr_in6 * server) {
// 	uint8_t count = 0;
// 	uint8_t recvPDU[MAXBUF] = {0};
// 	while(1) {
// 		if (pollCall(ONE_SECOND) != serverSocket) {
// 			count++;
// 			sendToServer(serverSocket, server, getPDU(0), PDU_HEADER_LEN + 7 + strlen(argv[FROM_FILE]));
// 		}
// 		else {
// 			recvFromServer(serverSocket, server, recvPDU);
// 			if (in_cksum((uint16_t *)recvPDU, PDU_HEADER_LEN + strlen(argv[FROM_FILE])) != VALID) {
// 				count++;
// 				sendToServer(serverSocket, server, getPDU(0), PDU_HEADER_LEN + 7 + strlen(argv[FROM_FILE]));
// 			}
// 			else if (recvPDU[6] == EST_RESPOND) {
// 				uint8_t *send_pdu = createPDU(0, EST_ACK, (uint8_t[]){EST_ACK}, 1);
// 				addPDU(send_pdu, 0, PDU_HEADER_LEN + 1);
// 				sendToServer(serverSocket, server, send_pdu, PDU_HEADER_LEN + 1);
// 				*current_state = OPEN_FILE;
// 				return;
// 			}
// 			else if (recvPDU[6] == FILE_DNE) {
// 				printf("file %s not found\n", argv[FROM_FILE]);
// 				*current_state = DONE;
// 				return;
// 			}
// 		}
// 		if (count > 9) {
// 			close(serverSocket);
// 			printf("ERROR: SERVER TIMEOUT\n");
// 			exit(1);
// 		}
// 	}
// }

// void initializeFile(State *current_state, uint8_t *filename, FILE **file) {
// 	*file = fopen((const char *)filename, "wb");
// 	if (!*file) {
// 		printf("Error on open of output file: %s\n", filename);
// 		*current_state = DONE;
// 		return;
// 	}
// 	*current_state = RECV_DATA;
// }

// void recvData(int serverSocket, char * argv[], State *current_state, struct sockaddr_in6 * server, FILE *file) {
// 	uint32_t expected = 0;
// 	uint32_t sequence_num = 1;
// 	uint32_t highest = 0;
// 	uint8_t eof_flag = 0;
// 	BufferState buffer_state = INORDER;
// 	while(1) {
// 		switch(buffer_state) {
// 			case INORDER:
// 				inorder(serverSocket, argv, &buffer_state, server, file, &expected, &sequence_num, &highest);
// 				break;
// 			case BUFFERING:
// 				buffering(serverSocket, argv, &buffer_state, server, file, &expected, &sequence_num, &highest);
// 				break;
// 			case FLUSHING:
// 				flushing(serverSocket, argv, &buffer_state, server, file, &expected, &sequence_num, &highest, eof_flag);
// 				break;
// 			case EOF_STATE:
// 				eof_flag = 1;
// 				if (highest == expected) {
// 					uint8_t *send_pdu = createPDU(sequence_num, FINAL_PACKET_EOF, (uint8_t[]){FINAL_PACKET_EOF}, 1);
// 					sendToServer(serverSocket, server, send_pdu, PDU_HEADER_LEN + 1);
// 					*current_state = DONE;
// 					return;
// 				}
// 				flushing(serverSocket, argv, &buffer_state, server, file, &expected, &sequence_num, &highest, eof_flag);
// 				buffering(serverSocket, argv, &buffer_state, server, file, &expected, &sequence_num, &highest);
// 				break;
// 			case TIMEOUT:
// 				printf("ERROR: SERVER TIMEOUT\n");
// 				fclose(file);
// 				close(serverSocket);
// 				exit(-1);
// 		}
// 	}
// }

// void inorder(int serverSocket, char * argv[], BufferState *buffer_state, struct sockaddr_in6 * server, FILE *file, uint32_t *expected, uint32_t *sequence_num, uint32_t *highest) {
// 	uint8_t count = 0;
// 	uint8_t recv_pdu[MAXBUF] = {0};
// 	int recv_data_len = 0;
// 	uint32_t recv_seq_num = 0;
// 	uint8_t *send_pdu = NULL;
// 	while(1) {
// 		if (pollCall(ONE_SECOND) == serverSocket) {
// 			recv_data_len = recvFromServer(serverSocket, server, recv_pdu);
// 			recv_seq_num = ntohl(*(uint32_t *)recv_pdu);
// 			if (in_cksum((uint16_t *)recv_pdu, recv_data_len) != VALID) {
// 				count++;
// 			}
// 			else if (recv_pdu[6] == REG_DATA_PACKET || recv_pdu[6] == RESENT_DATA_TIMEOUT) {
// 				if (recv_seq_num < *expected) {
// 					uint32_t expected_network = htonl(*expected);
// 					send_pdu = createPDU(*sequence_num, RR_PACKET, (uint8_t *)&expected_network, sizeof(expected_network));
// 					*sequence_num += 1;
// 				}
// 				else if (recv_seq_num == *expected) {
// 					fwrite(recv_pdu + PDU_HEADER_LEN, 1, recv_data_len - PDU_HEADER_LEN, file);
// 					fflush(file);
// 					setValidFlag(*expected, 0);
// 					*highest = *expected;
// 					*expected += 1;
// 					uint32_t expected_network = htonl(*expected);
// 					send_pdu = createPDU(*sequence_num, RR_PACKET, (uint8_t *)&expected_network, sizeof(expected_network));
// 					*sequence_num += 1;
// 				}
// 				else if (recv_seq_num >= *expected) {
// 					addPDU(recv_pdu, recv_seq_num, recv_data_len);
// 					setValidFlag(recv_seq_num, 1);
// 					uint32_t expected_network = htonl(*expected);
// 					send_pdu = createPDU(*sequence_num, SREJ_PACKET, (uint8_t *)&expected_network, sizeof(expected_network));
// 					*sequence_num += 1;
// 					*highest = recv_seq_num;
// 					*buffer_state = BUFFERING;
// 				}
// 				sendToServer(serverSocket, server, send_pdu, PDU_HEADER_LEN + 4);
// 				return;
// 			}
// 			else if (recv_pdu[6] == FINAL_PACKET_EOF) {
// 				setValidFlag(*expected, 0);
// 				*highest = *expected;
// 				*buffer_state = EOF_STATE;
// 				return;
// 			}
// 			else {
// 				count++;
// 			}
// 		}
// 		else count++;
// 		if (count > 9) {
// 			*buffer_state = TIMEOUT;
// 			return;
// 		}
// 	}
// }

// void buffering(int serverSocket, char * argv[], BufferState *buffer_state, struct sockaddr_in6 * server, FILE *file, uint32_t *expected, uint32_t *sequence_num, uint32_t *highest) {
// 	uint8_t count = 0;
// 	uint8_t recv_pdu[MAXBUF] = {0};
// 	int recv_data_len = 0;
// 	uint32_t recv_seq_num = 0;
// 	while(1) {
// 		if (pollCall(ONE_SECOND) == serverSocket) {
// 			recv_data_len = recvFromServer(serverSocket, server, recv_pdu);
// 			recv_seq_num = ntohl(*(uint32_t *)recv_pdu);
// 			if (in_cksum((uint16_t *)recv_pdu, recv_data_len) != VALID) {
// 				count++;
// 			}
// 			else if (recv_pdu[6] == REG_DATA_PACKET || recv_pdu[6] == RESENT_DATA_TIMEOUT || recv_pdu[6] == RESENT_DATA_SREJ) {
// 				if (recv_seq_num < *expected) {
// 					uint32_t expected_network = htonl(*expected);
// 					uint8_t *rr_pdu = createPDU(*sequence_num, RR_PACKET, (uint8_t *)&expected_network, sizeof(expected_network));
// 					*sequence_num += 1;
// 					sendToServer(serverSocket, server, rr_pdu, PDU_HEADER_LEN + 4);
// 				}
// 				else if (recv_seq_num == *expected) {
// 					fwrite(recv_pdu + PDU_HEADER_LEN, 1, recv_data_len - PDU_HEADER_LEN, file);
// 					fflush(file);
// 					setValidFlag(recv_seq_num, 0);
// 					*expected += 1;
// 					*buffer_state = FLUSHING;
// 				}
// 				else if (recv_seq_num >= *expected) {
// 					addPDU(recv_pdu, recv_seq_num, recv_data_len);
// 					setValidFlag(recv_seq_num, 1);
// 					*highest = recv_seq_num;
// 				}
// 				return;
// 			}
// 			else if (recv_pdu[6] == FINAL_PACKET_EOF) {
// 				*highest = recv_seq_num;
// 				*buffer_state = EOF_STATE;
// 				return;
// 			}
// 			else count++;
// 		}
// 		else count++;
// 		if (count > 9) {
// 			*buffer_state = TIMEOUT;
// 			return;
// 		}
// 	}
// }

// void flushing(int serverSocket, char * argv[], BufferState *buffer_state, struct sockaddr_in6 * server, FILE *file, uint32_t *expected, uint32_t *sequence_num, uint32_t *highest, uint8_t eof_flag) {
// 	while(*expected < *highest && getValidFlag(*expected) == 1) {
// 		uint8_t *buffered_pdu = getPDU(*expected);
// 		fwrite(buffered_pdu + PDU_HEADER_LEN, 1, getPduLen(*expected) - PDU_HEADER_LEN, file);
// 		fflush(file);
// 		setValidFlag(*expected, 0);
// 		*expected += 1;
// 	}
// 	if (*expected < *highest && getValidFlag(*expected) == 0) {
// 		uint32_t expected_network = htonl(*expected);
// 		uint8_t *srej_pdu = createPDU(*sequence_num, SREJ_PACKET, (uint8_t *)&expected_network, sizeof(expected_network));
// 		sendToServer(serverSocket, server, srej_pdu, PDU_HEADER_LEN + 4);
// 		*sequence_num += 1;
// 		uint8_t *rr_pdu = createPDU(*sequence_num, RR_PACKET, (uint8_t *)&expected_network, sizeof(expected_network));
// 		*sequence_num += 1;
// 		sendToServer(serverSocket, server, rr_pdu, PDU_HEADER_LEN + 4);
// 		*buffer_state = BUFFERING;
// 	}
// 	else if (*expected == *highest) {
// 		if (eof_flag) {
// 			*buffer_state = EOF_STATE;
// 			return;
// 		}
// 		uint8_t *buffered_pdu = getPDU(*expected);
// 		fwrite(buffered_pdu + PDU_HEADER_LEN, 1, getPduLen(*expected) - PDU_HEADER_LEN, file);
// 		fflush(file);
// 		setValidFlag(*expected, 0);
// 		*expected += 1;
// 		uint32_t expected_network = htonl(*expected);
// 		uint8_t *rr_pdu = createPDU(*sequence_num, RR_PACKET, (uint8_t *)&expected_network, sizeof(expected_network));
// 		*sequence_num += 1;
// 		sendToServer(serverSocket, server, rr_pdu, PDU_HEADER_LEN + 4);
// 		*buffer_state = INORDER;
// 	}
// }