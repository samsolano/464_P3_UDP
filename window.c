#include "window.h"

int windowSize = 0;
uint32_t lower = 0;
uint32_t current = 0;
uint32_t upper = 0;

uint32_t   getLower() { return lower;   }
uint32_t getCurrent() { return current; }
uint32_t   getUpper() { return upper;   }

void setLower(uint32_t new)   {  lower  = new; }
void setCurrent(uint32_t new) { current = new; }
void setUpper(uint32_t new)   {  upper  = new; }

void setupWindow(int windowLen) {

	windowEntries = (entry *) sCalloc(windowLen, sizeof(entry));
    windowSize = windowLen;
    upper = windowSize;

    memset(windowEntries[current].fullPdu, 0, MAXBUF);
}

void addToWindow(char *pdu, uint16_t pduLen) {

    if (windowEntries == NULL) {
        perror("Window needs to be setup");
        return;
    }
    memset(windowEntries[current].fullPdu, 0, MAXBUF);
	memcpy(windowEntries[current].fullPdu, pdu, pduLen);
	windowEntries[current].seq = 69;
    current++;
}

uint8_t *getWindowEntry(uint32_t seqNum) {

    uint32_t index = seqNum % (windowSize - 1);
    return windowEntries[index].fullPdu;
}

uint8_t windowOpen() {

    return upper - current;
}
