#ifndef WINDOW_H  // Check if not defined
#define WINDOW_H  // Define it

#include <stdint.h>
#include "safeUtil.h"
#include <string.h> 

#define MAXBUF 1407

uint32_t getLower();
uint32_t getCurrent();
uint32_t getUpper();
void setLower(uint32_t new);
void setCurrent(uint32_t new);
void setUpper(uint32_t new);
void setupWindow(int windowLen);
void addToWindow(char *pdu, uint16_t pduLen);
uint8_t *getWindowEntry(uint32_t seqNum);
uint8_t windowOpen();

typedef struct {
    uint8_t fullPdu[MAXBUF];
    uint8_t valid;
    uint32_t seq;
} entry;

entry *windowEntries;



#endif
