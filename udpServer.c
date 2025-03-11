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
#include "cpe464.h"

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
void teardown(int socketNum, struct sockaddr * client, int bufferSize);


void windowTesting();


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
	// uint8_t packet[bufferSize + 7];
	

	while (1) {
		
		
		while(windowOpen()) {
			// printf("in window open\n");
			// uint8_t packetPayload[bufferSize];
			// uint8_t dataPacket[bufferSize + 7];

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
	int packetType = 16;

	int bytes = read(file, packetPayload, bufferSize);												// fill payload with bytes from file

	if (bytes == 0) { printf("no more bytes\n"); exit(0); }
	else if(bytes == -1) { perror("Error reading file"); exit(-1); }
	else if (bytes < bufferSize) {
		//handle last packet
		printf("bytes < buffer, %d < %d\n", bytes, bufferSize);
		packetType = 10;
	}
	
	createPDU(dataPacket, packetPayload, bytes, packetType);												// fill packet with header and payload
	addToWindow((char *) dataPacket, bytes + 7, seqNum);										// add packet to payload
	

	safeSendto(socketNum, dataPacket, bytes + 7, 0, client, sizeof(struct sockaddr_in6)); 
	printf("%d: packet sent\n", seqNum);
	seqNum++;

	if (packetType == 10) {
		teardown(socketNum, client, bufferSize);
	}
}

void teardown(int socketNum, struct sockaddr * client, int bufferSize) {

	printAll();
	printWindowValues();
	
	// uint8_t sendPacket[bufferSize + 7];
	// uint8_t recvPacket[bufferSize + 7];
	// int addrLen = sizeof(client);
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

		// poll for a second
		// receive it
		// if rr set lower, if srej send packet back 
		// keep polling if nothing to receive, send lowest packet, if 10 fails quit
		// keep checking to see if flag 35 is received, if it is exit
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
		printf("\tRR packet for %d\n", responseNumberHost);
		setLower(responseNumberHost + 1);
	}
	else if (flag == 6) {		// SREJ
		printf("\tSREJ packet\n");
		resendPacket(responseNumberHost, socketNum, client, bufferSize);
	}
	else if (flag == 35) {
		printf("eof ack received, quitting now\n");
		exit(0);
	}
}


void resendPacket(int sequenceNumber, int socketNum, struct sockaddr *client, int bufferSize) {

	printf("Resending Packet %d\n", sequenceNumber);
	uint8_t dataPacket[bufferSize + 7];
	memcpy(dataPacket, getWindowEntry(sequenceNumber), getEntryLen(sequenceNumber));
	safeSendto(socketNum, dataPacket, getEntryLen(sequenceNumber), 0, client, sizeof(struct sockaddr_in6));
}


void sendWithRetries(int socketNum, void * buf, int len, struct sockaddr *srcAddr, int addrLen) {
	// int returnValue = 0;
	int numOfTries = 0;
	// int pollResult = 0;

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

	if (argc > 3 | error < 0.0 | error > 1.0)
	{
		fprintf(stderr, "Usage %s error-rate (0.0 - 1.0) [optional port number]\n", argv[0]);
		exit(-1);
	}
	
	if (argc == 3)
	{
		portNumber = atoi(argv[2]);
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













#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "gethostbyname.h"
#include "networks.h"
#include "safeUtil.h"
#include "window.h"
#include "cpe464.h"
#include "checksum.h"
#include "pollLib.h"
#include "helper.h"

typedef enum {
	FILENAME,
	SEND_DATA,
	ACK_WAIT,
	EOF_ACK_WAIT,
	DONE
} State;

uint8_t *recvFromRcopy(int socketNum, struct sockaddr_in6 *client);
void sendToRcopy(int socketNum, struct sockaddr_in6 *client, uint8_t *pdu, int pdu_len);
int checkArgs(int argc, char *argv[]);
void serverControl(int mainServerSocket);
void processRequest(int mainServerSocket, struct sockaddr_in6 *client, uint8_t * recv_pdu);
void serverFSM(uint32_t window_size, uint16_t buffer_size, uint8_t *filename, struct sockaddr_in6 *client);
uint8_t *initializeFile(State *current_state, uint8_t *filename, FILE **file);
uint8_t waitOnRcopy(int socketNum, struct sockaddr_in6 *client, uint8_t *send_pdu, int payload_len);
uint32_t sendData(int socketNum, struct sockaddr_in6 *client, FILE *file, uint16_t buffer_size, State *current_state);
uint8_t processAckPackets(int socketNum, struct sockaddr_in6 *client);
uint8_t processClosedWindow(int socketNum, struct sockaddr_in6 *client);
void waitEofAck(int socketNum, struct sockaddr_in6 *client, uint32_t sequence_number, State *current_state);

float error_rate;

int main ( int argc, char *argv[]  )
{ 
	int serverSocketNum = 0;
	int portNumber = 0;

	portNumber = checkArgs(argc, argv);

	sendtoErr_init(error_rate, DROP_ON, FLIP_ON, DEBUG_ON, RSEED_ON);

	serverSocketNum = udpServerSetup(portNumber);

	serverControl(serverSocketNum);

	close(serverSocketNum);

	return 0;
}

uint8_t *recvFromRcopy(int socketNum, struct sockaddr_in6 *client)
{
	int dataLen = 0;
	static uint8_t buffer[MAXBUF];
	int clientAddrLen = sizeof(client);
	
	buffer[0] = '\0';
	dataLen = safeRecvfrom(socketNum, buffer, MAXBUF, 0, (struct sockaddr *) client, &clientAddrLen);
	buffer[dataLen] = '\0';
	return buffer;
}

void sendToRcopy(int socketNum, struct sockaddr_in6 *client, uint8_t *pdu, int pdu_len) {
	safeSendto(socketNum, pdu, pdu_len, 0, (struct sockaddr *) client, sizeof(struct sockaddr_in6));
}

int checkArgs(int argc, char *argv[])
{
	// Checks args and returns port number
	int portNumber = 0;
	if (argc > 3 || argc < 2)
	{
		fprintf(stderr, "Usage %s error-rate [optional port number]\n", argv[0]);
		exit(-1);
	}

	error_rate = atof(argv[1]);

	if (argc == 3)
	{
		portNumber = atoi(argv[2]);
	}

	return portNumber;
}

void serverControl(int mainServerSocket) {
	struct sockaddr_in6 client;
	uint8_t recv_pdu[MAX_PDU] = {0};
	while (1) {
		memcpy(recv_pdu, recvFromRcopy(mainServerSocket, &client), MAX_PDU);
		processRequest(mainServerSocket, &client, recv_pdu);
	}
}

void processRequest(int mainServerSocket, struct sockaddr_in6 *client, uint8_t * recv_pdu) {
	uint8_t payload_pos = 7;
	if (recv_pdu[6] != EST_FLAG) {
		return;
	}
	uint32_t window_size = ntohl(*(uint32_t *)(recv_pdu + payload_pos));
	uint16_t buffer_size = ntohs(*(uint16_t *)(recv_pdu + payload_pos + 4));
	uint8_t filename_len = recv_pdu[payload_pos + 6];
	uint8_t filename[FILENAME_MAX_LEN] = {0};
	memcpy(filename, recv_pdu + payload_pos + 7, filename_len);
	if (in_cksum((uint16_t *)recv_pdu, PDU_HEADER_LEN + 7 + filename_len) != VALID) {
		return;
	}
	pid_t pid = fork();
	if (pid == 0) {// THIS IS CHILD AREA
		close(mainServerSocket);
		serverFSM(window_size, buffer_size, filename, client);
	}
}

void serverFSM(uint32_t window_size, uint16_t buffer_size, uint8_t *filename, struct sockaddr_in6 *client) {
	sendtoErr_init(error_rate, DROP_ON, FLIP_ON, DEBUG_ON, RSEED_ON);
	int child_socket = socket(AF_INET6, SOCK_DGRAM, 0);
	setupPollSet();
	addToPollSet(child_socket);
	createWindow(window_size);
	FILE *file = NULL;
	State current_state = FILENAME;
	uint8_t *send_pdu = NULL;
	uint32_t sequence_number = 0;
	uint8_t recv_flag;
	while(1) {
		switch(current_state) {
			case FILENAME:
				send_pdu = initializeFile(&current_state, filename, &file);
				sendToRcopy(child_socket, client, send_pdu, PDU_HEADER_LEN + strlen((const char *)filename));
				break;
			case SEND_DATA:
				sequence_number = sendData(child_socket, client, file, buffer_size, &current_state);
				break;
			case ACK_WAIT:
				recv_flag = waitOnRcopy(child_socket, client, send_pdu, strlen((const char *)filename));
				if (recv_flag == TIMEOUT_FLAG) {
					current_state = DONE;
				}
				else {
					current_state = SEND_DATA;
				}
				break;
			case EOF_ACK_WAIT:
				waitEofAck(child_socket, client, sequence_number, &current_state);
				break;
			case DONE:
				fclose(file);
				close(child_socket);
				freeWindow();
				exit(0);
				break;
		}
	}
}

uint32_t sendData(int socketNum, struct sockaddr_in6 *client, FILE *file, uint16_t buffer_size, State *current_state) {
	uint8_t *payload = (uint8_t *)sCalloc(buffer_size, sizeof(uint8_t));
	uint8_t send_pdu[MAX_PDU] = {0};
	uint32_t sequence_num = 0;
	size_t bytes_read = 0;
	while(1) {
		while(windowOpen() > 0) {
			bytes_read = fread(payload, 1, buffer_size, file);
			if (bytes_read == 0) {
				if (feof(file)) {
					payload[0] = FINAL_PACKET_EOF;
					memcpy(send_pdu, createPDU(sequence_num, FINAL_PACKET_EOF, payload, 1), sizeof(send_pdu));
					addPDU(send_pdu, sequence_num, PDU_HEADER_LEN + 1);
					while(getLower() < sequence_num) {
						if (processClosedWindow(socketNum, client) == TIMEOUT_FLAG) {
							*current_state = DONE;
						}
					}
					sendToRcopy(socketNum, client, send_pdu, PDU_HEADER_LEN + 1);
					*current_state = EOF_ACK_WAIT;
				}
				else if (ferror(file)) {
					printf("ERROR: FILE READ ERROR\n");
					*current_state = DONE;
				}
				free(payload);
				return sequence_num;
			}
			memcpy(send_pdu, createPDU(sequence_num, REG_DATA_PACKET, payload, (int)bytes_read), MAXBUF);
			addPDU(send_pdu, sequence_num, PDU_HEADER_LEN + bytes_read);
			sendToRcopy(socketNum, client, send_pdu, PDU_HEADER_LEN + bytes_read);
			setCurrent(getCurrent() + 1);
			sequence_num++;
			while (pollCall(0) == socketNum) {
				processAckPackets(socketNum, client);
			}
		}
		while (windowOpen() == 0) {
			if (processClosedWindow(socketNum, client) == TIMEOUT_FLAG) {
				printf("ERROR: WINDOW TIMEOUT\n");
				*current_state = DONE;
				free(payload);
				return sequence_num;
			}
		}
	}
}

uint8_t processClosedWindow(int socketNum, struct sockaddr_in6 *client) {
	static uint8_t count = 0;
	if (pollCall(ONE_SECOND) == socketNum) {
		if (processAckPackets(socketNum, client) != VALID) {
			count++;
		}
		else {
			count = 0;
		}
	}
	else {
		uint8_t *lowest_pdu = getPDU(getLower());
		uint8_t *resend_pdu = createPDU(getLower(), RESENT_DATA_TIMEOUT, lowest_pdu + PDU_HEADER_LEN, getPduLen(getLower()));
		sendToRcopy(socketNum, client, resend_pdu, getPduLen(getLower()));
		count++;
	}
	if (count > 9) {
		return TIMEOUT_FLAG;
	}
	return CHECK_WINDOW;
}

uint8_t processAckPackets(int socketNum, struct sockaddr_in6 *client) {
	uint8_t *recv_pdu = recvFromRcopy(socketNum, client);
	uint8_t *resend_pdu = NULL;
	uint8_t *original_pdu = NULL;
	uint32_t recv_seq_num = 0;
	if (in_cksum((uint16_t *)recv_pdu, PDU_HEADER_LEN + 4) != VALID) {
		return !VALID;
	}
	if (recv_pdu[6] == RR_PACKET) {
		recv_seq_num = ntohl(*(uint32_t *)(recv_pdu + 7));
		setLower(recv_seq_num);
		setUpper(getLower() + getWindowSize());
		validateWindow(recv_seq_num);
	}
	else if (recv_pdu[6] == SREJ_PACKET) {
		recv_seq_num = ntohl(*(uint32_t *)(recv_pdu + 7));
		original_pdu = getPDU(recv_seq_num);
		resend_pdu = createPDU(recv_seq_num, RESENT_DATA_SREJ, original_pdu + PDU_HEADER_LEN, getPduLen(recv_seq_num));
		sendToRcopy(socketNum, client, resend_pdu, getPduLen(recv_seq_num));
	}
	return VALID;
}

uint8_t waitOnRcopy(int socketNum, struct sockaddr_in6 *client, uint8_t *send_pdu, int payload_len) {
	uint8_t count = 0;
	uint8_t *recvPDU = NULL;
	while(1) {
		if (pollCall(ONE_SECOND) != socketNum) {
			count++;
			sendToRcopy(socketNum, client, send_pdu, PDU_HEADER_LEN + payload_len);
		}
		else {
			recvPDU = recvFromRcopy(socketNum, client);
			if (in_cksum((uint16_t *)recvPDU, PDU_HEADER_LEN + 1) != VALID) {
				count++;
				sendToRcopy(socketNum, client, send_pdu, PDU_HEADER_LEN + payload_len);
			}
			else {
				return recvPDU[6];
			}
		}
		if (count > 9) {
			return TIMEOUT_FLAG;
		}
	}
}

void waitEofAck(int socketNum, struct sockaddr_in6 *client, uint32_t sequence_number, State *current_state) {
	if (waitOnRcopy(socketNum, client, getPDU(sequence_number), getPduLen(sequence_number) - PDU_HEADER_LEN) == TIMEOUT_FLAG) {
		printf("ERROR: EOF ACK TIMEOUT\n");
		*current_state = DONE;
		return;
	}
	*current_state = DONE;
	return;
}

uint8_t *initializeFile(State *current_state, uint8_t *filename, FILE **file) {
	static uint8_t pdu[MAX_PDU] = {0};
	pdu[0] = '\0';
	*file = fopen((const char *)filename, "rb");
	if (!*file) {
		*current_state = DONE;
		memcpy(pdu, createPDU(0, FILE_DNE, filename, strlen((const char *)filename)), MAXBUF);
	}
	else {
		*current_state = ACK_WAIT;
		memcpy(pdu, createPDU(0, EST_RESPOND, filename, strlen((const char *)filename)), MAXBUF);
	}
	return pdu;
}