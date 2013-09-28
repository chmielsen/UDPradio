#ifndef _COMMON_
#define _COMMON_

#include <string.h>
#include <stdio.h>

#include "err.h"

#define ALBUM_NR 5187
#define MAX_PACKAGE_NR_LENGTH 12

#define TRUE 1
#define FALSE 0

static unsigned short DATA_PORT = 20000 + ALBUM_NR;
static unsigned short CTRL_PORT = 30000 + ALBUM_NR;
static unsigned short RTIME     = 250;

static const char ID_REQUEST[] = "RequestTransmitterID";
static const char RETRANSMIT_REQUEST[] = "Retransmit"; // and number of the package after it


/* extract options */
inline char* extract(char arg, int argc, char* argv[]) {
    int i;
    char requested[] = {'-', '\0', '\0'};
    requested[1] = arg;
    for(i = 0; i < argc; i++) {
        if(strcmp(requested, argv[i]) == 0) {
            if(i + 1 >= argc) 
                fatal("Init: Argument -%c was not given\n", arg);
            return argv[i + 1]; 
       }
    }
    /* Not found */
    return NULL;
}

#endif
