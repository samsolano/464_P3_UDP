#include "window.h"

int windowSize = 0;
uint32_t lower = 0;
uint32_t current = 0;
uint32_t upper = 0;

uint32_t   getLower() { return lower;   }
uint32_t getCurrent() { return current; }
uint32_t   getUpper() { return upper;   }


void setLower(uint32_t newLower) { 

    if (windowEntries == NULL) {
        perror("Window is not setup");
        return;
    }
    if(newLower >= upper) {
        perror("Upper bound exceeded");
        return;
    }

    uint32_t timesToShift = newLower - lower;

    // Shift all elements to the left
    for (int i = 0; i < windowSize; i++) {
        if((i + timesToShift) >= windowSize) {
            memset(&windowEntries[i],0, sizeof(entry));
        }
        else {
            memcpy(&windowEntries[i], &windowEntries[i + timesToShift], sizeof(entry));
        }
    }

    lower = newLower;
    upper = lower + windowSize;
}

void setCurrent(uint32_t new) { current = new; }
void setUpper(uint32_t new)   {  upper  = new; }

void setupWindow(int windowLen) {

	windowEntries = (entry *) sCalloc(windowLen, sizeof(entry));
    windowSize = windowLen;
    upper = windowSize;
}

void addToWindow(char *pdu, uint16_t pduLen, int seqNum) {

    if (windowEntries == NULL) {
        perror("Window needs to be setup");
        return;
    }
    uint32_t index = seqNum - lower;

    memset(windowEntries[index].fullPdu, 0, MAXBUF);
	memcpy(windowEntries[index].fullPdu, pdu, pduLen);
	windowEntries[seqNum - lower].seq = seqNum;
    windowEntries[index].valid = 1;
    current++;
}

uint8_t *getWindowEntry(uint32_t seqNum) {

    if(seqNum >= upper || seqNum < lower) {
        perror("Number out of window bounds");
        return NULL;
    }
    uint32_t index = seqNum - lower;
    return windowEntries[index].fullPdu;
}

uint8_t windowOpen() {

    return upper - current;
}


void printAll() {

    printf("\nPrinting all\n");
	for(int i = lower; i < upper; i++) {

        printf("%d: ", i);
        for(int j = 0; j < 7; j++) {

            printf("%02x ", getWindowEntry(i)[j]);
        }

		printf("    valid: %d, seq: %d, address: %p\n", windowEntries[i - lower].valid, windowEntries[i - lower].seq, &windowEntries[i - lower]);
	}
	printf("\n");
}

void printWindowValues() {
    printf("\nLower: %d, current: %d, upper: %d\n", lower, current, upper);
}