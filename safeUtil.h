// 
// Writen by Hugh Smith, April 2020
//
// Put in system calls with error checking.

#ifndef __SAFEUTIL_H__
#define __SAFEUTIL_H__

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdlib.h>

struct sockaddr;

int safeRecvfrom(int socketNum, void * buf, int len, int flags, struct sockaddr *srcAddr, int * addrLen);
int safeSendto(int socketNum, void * buf, int len, int flags, struct sockaddr *srcAddr, int addrLen);
int safeRecv(int socketNum, void * buf, int len, int flags);
int safeSend(int socketNum, void * buf, int len, int flags);

void * srealloc(void *ptr, size_t size);
void * sCalloc(size_t nmemb, size_t size);


#endif
