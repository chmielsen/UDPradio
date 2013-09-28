#ifndef _RADIO
#define _RADIO

#include <string.h>
#include <netinet/in.h>

#define BUFFER 20
#define MAX_PLAYING_RADIOS 100

typedef struct {
    char* name;
    char* version;
    char* mcast_addr;
    char* prog_name;
} RadioID;

typedef struct {
    RadioID station;
    /* if warnings > 3 then we should remove Radio from list */
    int warnings; 
    struct sockaddr_in address;
} BroadcastingRadio;

typedef struct {
    size_t size;
    BroadcastingRadio radio[MAX_PLAYING_RADIOS];
    int playingRadioInd;
} RadioList;


/* str has to have format:
 * PROG_NAME" MCAST_ADDR VERSION "NAME e.g:
 * "nadajnik" 234.11.255.2255 v:1.0 "Nienazawany Nadajnik"
 * Returns -1 if str is not applying to restrictions above
 * WARNING: FUNCTION MODIFIES str (using strtok) */
int radioFromStr(char* str, RadioID* radioID) {
    char* pch;
    char mcast[BUFFER];
    char ver[BUFFER];

    pch = strtok(str, "\"");
    /* First tok should contain PROG_NAME */
    if(pch == NULL || strlen(pch) == 0)
        return -1;
    radioID->prog_name = (char*) malloc(strlen(pch) * sizeof(char));
    strcpy(radioID->prog_name, pch);
    pch = strtok(NULL, "\"");
    /* Second tok should contain MCAST_ADDR and VERSION */
    if(pch == NULL || strlen(pch) == 0) {
        free(radioID->prog_name);
        return -1;
    }
    if(sscanf(pch, "%s %s", mcast, ver) < 2) {
        free(radioID->prog_name);
        return -1;
    }
    radioID->mcast_addr = (char*) malloc(strlen(mcast) * sizeof(char));
    radioID->version = (char* ) malloc(strlen(ver) * sizeof(char));
    strcpy(radioID->mcast_addr, mcast);
    strcpy(radioID->version, ver);
    pch = strtok(NULL, "\"");
    /* Third tok should contain NAME */
    if(pch == NULL || strlen(pch) == 0) {
        free(radioID->prog_name);
        free(radioID->mcast_addr);
        free(radioID->version);
        return -1;
    }
    radioID->name = (char*) malloc(strlen(pch) * sizeof(char));
    strcpy(radioID->name, pch);
    return 0;
}

inline void destroyRadioEntry (RadioID* radio) {
    free(radio->prog_name);
    free(radio->mcast_addr);
    free(radio->version);
    free(radio->name);
}

inline void printRadio(RadioID* radio) {
    printf("Radio:\n\tname: %s\n\tmcast_addr: %s\n"
           "\tversion: %s\n\tprog_name: %s\n",
           radio->name, radio->mcast_addr, 
           radio->version, radio->prog_name);
}

inline void insertRadio(RadioList* radioList, RadioID* radio, struct sockaddr_in* address) {
    size_t n;
    
    n = radioList->size;
    if(n == MAX_PLAYING_RADIOS)
        return;
    radioList->radio[n].station = *radio;
    radioList->radio[n].warnings = 0;
    if(address != NULL)
        radioList->radio[n].address = *address;
    else memset(&radioList->radio[n].address, 0, sizeof(struct sockaddr_in));
    radioList->size++;
}

inline int radioCmp(RadioID r1, RadioID r2) {
    return ((strcmp(r1.name, r2.name) == 0) && 
            (strcmp(r1.version, r2.version) == 0) &&
            (strcmp(r1.mcast_addr, r2.mcast_addr) == 0) &&
            (strcmp(r1.prog_name, r2.prog_name) == 0)) - 1;
}

/* return index od radio, or -1 if not found */
inline int seekRadio(RadioList* radioList, RadioID* radio) {
    size_t i;

    for(i = 0; i < radioList->size; i++) {
        if(radioCmp(radioList->radio[i].station, *radio) == 0)
            return i;
    }
    return -1;
}

/* O(n) complexity */
inline void deleteRadio(RadioList* radioList, size_t index) {
    int shift;

    if(index >= radioList->size)
        return;
    shift = (int)radioList->size - index;
    destroyRadioEntry(&radioList->radio[index].station);
    memmove(&radioList->radio[index], &radioList->radio[index + 1], shift * sizeof(BroadcastingRadio));
    radioList->size--;
}

#undef BUFFER


#endif // _RADIO
