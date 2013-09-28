#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <poll.h>
#include <netdb.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <math.h>
#include <limits.h>

#include "err.h"
#include "common.h"
#include "radio.h"
#include "radiobuffer.h"

#define DEBUG 0

#define TTL_VALUE 5
#define RADIO_ID_SIZE 200

#define START_PACKAGES_COUNT 6
/* interval in micro seconds */
#define START_SEND_INTERVAL_US 500000
/* interval in seconds */
#define NORMAL_SEND_INTERVAL_S 5

#define MAX_WARN 4

/* Networks probably would block bigger */
#define MAX_MSG_SIZE 1024

#define MAX_MENU_SIZE 1000

#define BUFFER_FILLED 0.75
#define BUFFER_READY_TO_PLAY 1
#define BUFFER_NOT_READY 0

/* size of message for interface thread */
#define UI_BUF 10


/* Constants for Telnet */
/* sets telnet in char read mode */
const unsigned char TELNETMAGIC[] = {(unsigned char)255, 
    (unsigned char)251, (unsigned char)3, 0};
const char UPARROW[] = {27, 91, 65, 0};
const char DOWNARROW[] = {27, 91, 66, 0};


char DISCOVER_ADDR[] = "255.255.255.255";
unsigned short UI_PORT = 10000 + ALBUM_NR;
size_t BSIZE = 64 * 1024;


int outputWaiting;
int radioListChanged;

struct OutputThreadData {
    Buffer* bPtr;
    int readFd;
};


RadioList radioList;
Buffer* songBuffer;

pthread_t bufferThread;

/* used in blocking access to radioList*/
pthread_mutex_t radioListMutex = PTHREAD_MUTEX_INITIALIZER;

/* used in blocking access to buffer */
pthread_mutex_t bufferMutex = PTHREAD_MUTEX_INITIALIZER;

/* Creating beatiful radio menu */
void sprintMenu(char* data) {
    size_t i;
    char temp[50];
    for(i = 0; i < 50; i++)
        strcat(data, "\n");
    strcat(data , "*********************************************\n");
    for(i = 0; i < radioList.size; i++) {
        if(radioList.playingRadioInd == i)
            sprintf(temp, "| > |%40s|\n", radioList.radio[i].station.name);
        else sprintf(temp, "|%43s|\n", radioList.radio[i].station.name);
        strcat(data, temp);
    }
        strcat(data, "+++++++++++++++++++++++++++++++++++++++++++++\n");
}

void* handleBuffer(void*);


/* Referesh radioList, check if someone need to be expelled */
void addWarnsAndClean() {
    size_t i;
    int cancelled;
    
    cancelled = 0;
    pthread_mutex_lock(&radioListMutex);
    for(i = 0; i < radioList.size; i++) {
        if(++(radioList.radio[i].warnings) > MAX_WARN) {
            if(radioList.playingRadioInd == i) {
                /* Removing radio, which is playing */
                pthread_cancel(bufferThread);
                cancelled = 1;
                radioList.playingRadioInd = -1;
            }
            deleteRadio(&radioList, i);
            radioListChanged = _POSIX_OPEN_MAX + 1;
        }
    }
    if(cancelled) {
        if(pthread_join(bufferThread, NULL) != 0)
            syserr("Buffer Thread: Join Thread");
        if(radioList.size > 0) {
            radioList.playingRadioInd = 0;
            /* Playing new one */
            pthread_create(&bufferThread, 0, handleBuffer, NULL);
        }
    }
    pthread_mutex_unlock(&radioListMutex);
}

/* Thread responsible for responding to TCP requests on 
 * UI_PORT. Modified for telnet use */
/* Using code from 6th labs, comments unchanged */
void* handleInterface(void* data) {
    struct pollfd client[_POSIX_OPEN_MAX];
    int initialized[_POSIX_OPEN_MAX];
    struct sockaddr_in server;
    char buf[UI_BUF];
    char menuMsg[MAX_MENU_SIZE];
    int rval;
    int msgsock, activeClients, i, ret;
    int finish;
    int change;

    finish = FALSE;
    /* Inicjujemy tablicę z gniazdkami klientów, client[0] to gniazdko centrali */
    for (i = 0; i < _POSIX_OPEN_MAX; ++i) {
        client[i].fd = -1;
        client[i].events = POLLIN;
        client[i].revents = 0;
        initialized[i] = 0;
    }
    activeClients = 0;

    /* Tworzymy gniazdko centrali */
    client[0].fd = socket(PF_INET, SOCK_STREAM, 0);
    if (client[0].fd < 0) {
    syserr("Opening stream socket");
    }

    /* Co do adresu nie jesteśmy wybredni */
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    server.sin_port = htons(UI_PORT);
    if (bind(client[0].fd, (struct sockaddr*)&server,
            (socklen_t)sizeof(server)) < 0) {
        syserr("Binding stream socket");
    }

    /* Zapraszamy klientów */
    if (listen(client[0].fd, 5) == -1) {
        syserr("Starting to listen");
    }
    
    /* Do pracy */
    do {
        for (i = 0; i < _POSIX_OPEN_MAX; ++i)
        client[i].revents = 0;
        if (finish == TRUE && client[0].fd >= 0) {
            if (close(client[0].fd) < 0)
            syserr("close");
            client[0].fd = -1;
        }

        /* Czekamy przez 5000 ms */
        ret = poll(client, _POSIX_OPEN_MAX, 1000);
        if (ret < 0)
            syserr("poll");
        else if (ret > 0) {
            if (finish == FALSE && (client[0].revents & POLLIN)) {
                msgsock = accept(client[0].fd, (struct sockaddr*)0, (socklen_t*)0);
                if (msgsock == -1)
                    syserr("accept");
                else {
                    for (i = 1; i < _POSIX_OPEN_MAX; ++i) {
                        if (client[i].fd == -1) {
                            if(msgsock == 0) {
                                exit(1);
                            }
                            client[i].fd = msgsock;
                            activeClients += 1;
                            break;
                        }
                    }
                    if (i >= _POSIX_OPEN_MAX) {
                        #if DEBUG == 1
                            fprintf(stderr, "Too many clients\n");
                        #endif
                        if (close(msgsock) < 0)
                            syserr("close");
                    }
                }
            }
            for (i = 1; i < _POSIX_OPEN_MAX; ++i)
                if(client[i].fd != -1 && !initialized[i]) {
                    write(client[i].fd, TELNETMAGIC, sizeof TELNETMAGIC);
                    initialized[i] = 1;
                }
            for (i = 1; i < _POSIX_OPEN_MAX; ++i) {
                memset(menuMsg, 0, MAX_MENU_SIZE);
                if (client[i].fd != -1
                    && (client[i].revents & (POLLIN | POLLERR))) {
                    rval = read(client[i].fd, buf, UI_BUF);
                    if (rval < 0) {
                        syserr("Reading stream message");
                        if (close(client[i].fd) < 0)
                            syserr("close");
                        client[i].fd = -1;
                        activeClients -= 1;
                    }
                    else if (rval == 0) {
                        if (close(client[i].fd) < 0)
                            syserr("close");
                        client[i].fd = -1;
                        activeClients -= 1;
                    }
                    else if (rval == 3) {
                        /* UGLY UGLY */
                        buf[3] = '\0';
                        change = 0;
                        if(strcmp(buf, UPARROW) == 0)
                            change = -1;
                        else if(strcmp(buf, DOWNARROW) == 0)
                            change = 1;
                 
                        if((change == -1 && radioList.playingRadioInd != 0) ||
                           (change == 1 && radioList.playingRadioInd + 1 < radioList.size)){
                            /* There is possible change */
                            pthread_mutex_lock(&radioListMutex);
                            if(radioList.size > 1) {
                                /* Cancelling thread which currently prints radio music */
                                if(pthread_cancel(bufferThread) != 0)
                                    syserr("UI thread: cancel buffer thread");
                                if(pthread_join(bufferThread, 0) != 0)
                                    syserr("UI thread: join buffer thread");

                                radioList.playingRadioInd += change; 
                                sprintMenu(menuMsg);
                                #if DEBUG == 1
                                    fprintf(stderr,"Zmieniam stacje na: %d\n", radioList.playingRadioInd);
                                #endif
                                /* creating new, fresh thread for new, better radio */
                                if(pthread_create(&bufferThread, 0, handleBuffer, NULL) != 0)
                                    syserr("UI thread: create new buffer thread");
                            } else pthread_mutex_unlock(&radioListMutex);
                            radioListChanged = i;
                        } else {
                            pthread_mutex_lock(&radioListMutex);
                            sprintMenu(menuMsg);
                            pthread_mutex_unlock(&radioListMutex);
                        }
                        /* Sending menu back */
                        write(client[i].fd, menuMsg, strlen(menuMsg));
                    }
                }
           }
        if(radioListChanged) {
            /* We have to send new menu to clients */
            memset(menuMsg, 0, MAX_MENU_SIZE);
            pthread_mutex_lock(&radioListMutex);
            sprintMenu(menuMsg);
            pthread_mutex_unlock(&radioListMutex);
            for(i = 1; i < _POSIX_OPEN_MAX; ++i) {
                if(i != radioListChanged && client[i].fd != -1) {
                    /* Adding mutex lock, because there is (very little, but still)
                     * chance, that we will have SISEGV if not, e.g. radio is being
                     * deleted, while we are writing */
                    write(client[i].fd, menuMsg, strlen(menuMsg));
                }
            }
            radioListChanged = 0;
        }
        } else if (radioListChanged) {
            for(i = 1; i < _POSIX_OPEN_MAX; ++i)
                if(client[i].fd != -1) {
                    // refreshing menu
                    memset(menuMsg, 0, MAX_MENU_SIZE);
                    /* Adding mutex lock, because there is (very little, but still)
                     * chance, that we will have SISEGV if not, e.g. radio is being
                     * deleted, while we are writing */
                    pthread_mutex_lock(&radioListMutex);
                    sprintMenu(menuMsg);
                    pthread_mutex_unlock(&radioListMutex);
                            if(msgsock == 0) {
                                exit(1);
                            }
                     
                    write(client[i].fd, menuMsg, strlen(menuMsg));
                }
            radioListChanged = 0;
        }
    } while (finish == FALSE || activeClients > 0);

    if (client[0].fd >= 0)
        if (close(client[0].fd) < 0)
            syserr("Closing main socket");
    return NULL;
}

/* Thread responsible for emitting ID requests */
void* handleEmitting(void* ctrlSockPtr) {
    int starterPackages;
    int flags;
    size_t length;

    /* variables used in network communication */
    struct sockaddr_in discover_address;
    socklen_t snda_len;
    int ctrlSock;

    ctrlSock = *((int*)ctrlSockPtr);
    
    #if DEBUG == 1
        fprintf(stderr,"Diag:\n\tStarting thread responsible for EMITTING ID REQUESTS.\n"
               "\tThread number = %lu, PID = %d\n", (unsigned long)pthread_self(), getpid());
    #endif

    /* preparing discover_address */
    discover_address.sin_family = AF_INET;
    discover_address.sin_port = htons(CTRL_PORT);
    if(inet_aton(DISCOVER_ADDR, &discover_address.sin_addr) == 0)
        syserr("Init: inet_aton DISCOVER_ADDR");
    snda_len = sizeof(discover_address);
    length = strlen(ID_REQUEST);

    starterPackages = START_PACKAGES_COUNT;
    /* START_MODE - send 6 packages with 0.5 interval */

    flags = 0;
    while(starterPackages--) {
        if(sendto(ctrlSock, ID_REQUEST, length, flags, 
                    (struct sockaddr*) &discover_address, snda_len) != length)
            syserr("Emitter thread starting: sendto");
        addWarnsAndClean();
        usleep(START_SEND_INTERVAL_US); 
    }
    /* End of START_MODE */
    #if DEBUG == 1
        fprintf(stderr,"Diag:\n\tSuccessfully send %d starter packages.\n", START_PACKAGES_COUNT);
    #endif

    /* NORMAL_MODE is now on, trasmitting package with 5s interval */
    for(;;) {
        if(sendto(ctrlSock, ID_REQUEST, length, flags, 
                    (struct sockaddr*) &discover_address, snda_len) != length)
            syserr("Emitter thread: sendto");
        addWarnsAndClean();
        sleep(NORMAL_SEND_INTERVAL_S); 
    }
    
}

/* Returns BUFFER_READY_TO_PLAY if buffer has more or equal than
 * BUFFER_FILLED chunks */
int checkBuffer(Buffer* songBuffer) {
    if (((double)songBuffer->counter / songBuffer->size) < BUFFER_FILLED)
        return BUFFER_NOT_READY;
    return BUFFER_READY_TO_PLAY;
}

/* Thread responsible for outputting song
 * threadData should be OutputThreadData* type */
void* handleOutput(void* threadData) { 
    char dummy[10];
    char* content;

    int readFd;
    //size_t i;

    #if DEBUG == 1
        fprintf(stderr,"Diag:\n\tStarting thread responsible for OUTPUTTING.\n"
               "\tThread number = %lu, PID = %d\n", (unsigned long)pthread_self(), getpid());
    #endif

    readFd= ((struct OutputThreadData*)threadData)->readFd;
    if(songBuffer == NULL)
        pthread_exit(0);

    content = (char*) malloc(songBuffer->psize * sizeof(char));
    pthread_cleanup_push(free, (void*)content);
    memset(content, 0, songBuffer->psize);

    for(;;) {
        outputWaiting = TRUE;
        /* Hanging thread, bufferThread will send sth to us when 
         * buffer is ready */
        if(read(readFd, dummy, sizeof dummy) < 0)
            syserr("Output thread work: read");
        /* Emptying buffer */
        /* This temporarily prevents buffer from inserting new chunks, nu
         * if there would be case of buffer overflow, then thread might pursue
         * to write at songBuffer->start. The delay is minimal, since I/O in C
         * are lightning fast -  full 64kB buffer is written to file in ~0.005s */
        pthread_mutex_lock(&bufferMutex);
        if(songBuffer != NULL) {
            while(popChunk(songBuffer, content) ==  0) {
                fwrite(content, 1, songBuffer->psize, stdout);
                memset(content, 0, songBuffer->psize);
            }
        }
        pthread_mutex_unlock(&bufferMutex);
    }
    pthread_cleanup_pop(1);
    pthread_exit(NULL);
}


/* Checking if message is in the right format */
int properMessage(char* msgData, int len) {
    int messageNr;
    int messageNrLen;
    int i;

    if(sscanf(msgData, "%d", &messageNr) != 1)
        return -1;
    if(messageNr <= 0)
        return -1;
    if(len < MAX_PACKAGE_NR_LENGTH)
        return -1;
    messageNrLen = floor(log10(messageNr));
    for(i = 0; i < MAX_PACKAGE_NR_LENGTH - messageNrLen - 1; i++) 
        /* checking if first characters are spaces */
        if(msgData[i] != ' ') {
            return -1;
        }
    return 0;
}


/* Routines for pthread_cleanup */
void closeSocket(void* sock) {
    if(close(*(int*)sock) != 0 )
        syserr("Cleanup: close socket");
}

void cleanBufferMemory(void* buffer) {
    destroyBuffer((Buffer*) buffer);   
}

void cleanBufferPointer(void* buffer) {
    free(*(Buffer**)buffer);
    *(Buffer**)buffer = NULL;
}

void cleanThreadMemory(void* thread) {
    if(pthread_cancel(*(pthread_t*)thread) != 0)
        syserr("Cleanup: cancel thread");
    if(pthread_join(*(pthread_t*)thread, NULL) != 0)
        syserr("Cleanup: join thread");
}

/* comparing addresses */
int addrComp(struct sockaddr_in s1, struct sockaddr_in s2) {
    if((s1.sin_family == s2.sin_family) &&
      (s1.sin_addr.s_addr == s2.sin_addr.s_addr))
            return 0;
    else return 1;
            
}

/* argument is index of playing radio */
void* handleBuffer(void* ARG) {

    /* used when signalizing to handleOutput thread, that it can 
     * print now */
    int fd[2];

    char msgData[MAX_MSG_SIZE];
    char randomMsg[] = "so random";
    int dataSock, playingIndex;
    int chunkNr, flags;
    int rcv_len;
    size_t chunkCount, chunkSize;
    BroadcastingRadio radio;
    
    struct timeval rcv_time;
    struct sockaddr_in local_address, rcv_address;
    struct ip_mreq ip_mreq;
    struct OutputThreadData outputThreadData; 
    socklen_t rcva_len;

    pthread_t outputThread;

    #if DEBUG == 1
        fprintf(stderr,"Diag:\n\tStarting thread responsible for READING TO BUFFER.\n"
               "\tThread number = %lu, PID = %d\n", (unsigned long)pthread_self(), getpid());
    #endif

    playingIndex = radioList.playingRadioInd;
    radio = radioList.radio[playingIndex];
    pthread_mutex_unlock(&radioListMutex);

    /* opening dataSocket */
    dataSock = socket(AF_INET, SOCK_DGRAM, 0);
    if(dataSock < 0)
        syserr("Buffer init: dataSock");

    pthread_cleanup_push(closeSocket, (void*)&dataSock);

    /* adding ourselves to multicast group */
    ip_mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    if(inet_aton(radio.station.mcast_addr, &ip_mreq.imr_multiaddr) == 0)
        syserr("Buffer init: inet_aton ");
    if(setsockopt(dataSock, SOL_IP, IP_ADD_MEMBERSHIP, (void*)&ip_mreq, sizeof(ip_mreq)) < 0)
        syserr("Buffer init: setsockopt");

    /* binding to local address and port */
    local_address.sin_family = AF_INET;
    local_address.sin_addr.s_addr = htonl(INADDR_ANY);
    local_address.sin_port = htons(DATA_PORT);
    if(bind(dataSock, (struct sockaddr *)&local_address, sizeof(local_address)) < 0)
        syserr("Buffer init: bind");

    memset(msgData, 0, sizeof(msgData));

    /* Now we want to recevie one package, check it's size (UDP packages
     * always come in one piece), then create buffer and separate thread
     * to printing on stdout */
    
    flags = 0;
    rcva_len = (socklen_t)sizeof(rcv_address);
    do {
        rcv_len = recvfrom(dataSock, msgData, sizeof(msgData), flags,
                (struct sockaddr*)&rcv_address, &rcva_len);
        if(rcv_len < 0)
            syserr("Buffer start: read first message");
    } while(properMessage(msgData, rcv_len) < 0 ||
            addrComp(rcv_address, radio.address) != 0);
    gettimeofday(&rcv_time, NULL);
    /* We have our message! */
    /* Size of the raw data we have to output */
    chunkSize = rcv_len - MAX_PACKAGE_NR_LENGTH; 
    /* How many chunks are in buffer */
    chunkCount = BSIZE / chunkSize;

    songBuffer = (Buffer*) malloc(sizeof(songBuffer));
    
    pthread_cleanup_push(cleanBufferPointer, (void*)&songBuffer);
    initBuffer(songBuffer, chunkCount, chunkSize);
    /* Adding routines on cleanup, so we wont have memory leaks after thread,
     * cancelling */
    pthread_cleanup_push(cleanBufferMemory, (void*)songBuffer);

    chunkNr = 0;
    sscanf(msgData, "%d", &chunkNr);
    //printf("DIAG: INSERTING [%s]\n", msgData  + MAX_PACKAGE_NR_LENGTH + 1);
    insertChunk(songBuffer, msgData + MAX_PACKAGE_NR_LENGTH + 1, chunkNr, rcv_time);
    #if DEBUG == 1
        fprintf(stderr,"Diag:\n\tChunk size: %zu\n"
               "\tChunk in buffers: %zu\n"
               "\tBuffer size: %zu\n", chunkSize,
               chunkCount, BSIZE);
    #endif

    if(pipe(fd) < 0)
        syserr("Buffer start: pipe");

    outputThreadData.bPtr = songBuffer;
    outputThreadData.readFd = fd[0];

    if(pthread_create(&outputThread, 0, handleOutput, (void *)&outputThreadData) < 0)
        syserr("Buffer start: creating thread for writing output");
    /*
    if(pthread_create(&retransThread, 0, handleRetransmission, (void *)outputThreadData) < 0)
        syserr("Buffer start: creating thread for retransmission");
    */

    /* Cancelling our threads on cleanup */
    pthread_cleanup_push(cleanThreadMemory, (void*)&outputThread);

    /* Receving data */
    flags = 0;
    for(;;) {
        do {
            memset(msgData, 0, sizeof(msgData));
            rcv_len = recvfrom(dataSock, msgData, sizeof(msgData), flags,
                (struct sockaddr*)&rcv_address, &rcva_len);
            if(rcv_len < 0)
                syserr("Buffer start: read first message");
        } while((properMessage(msgData, rcv_len) < 0) &&
                addrComp(rcv_address, radio.address) != 0);
        gettimeofday(&rcv_time, NULL);
        sscanf(msgData, "%d", &chunkNr);

        /* May block receving data, see comment in handleOutput */
        pthread_mutex_lock(&bufferMutex);
        insertChunk(songBuffer, msgData + MAX_PACKAGE_NR_LENGTH + 1, chunkNr, rcv_time);
        pthread_mutex_unlock(&bufferMutex);
        if(outputWaiting == TRUE && checkBuffer(songBuffer) == BUFFER_READY_TO_PLAY) {
            outputWaiting = FALSE;
            if(write(fd[1], randomMsg, sizeof randomMsg) < 0)
                syserr("Buffer work: write");
        }
    }
    /* POSIX requirements, since we used cleanup_push */
    pthread_cleanup_pop(1);
    pthread_cleanup_pop(1);
    pthread_cleanup_pop(1);
    pthread_cleanup_pop(1);
}



int main(int argc, char* argv[]) {

    int ctrlSock;
    int optval, flags;
    int radioIndex;
    int length;

    RadioID newRadio;

    struct sockaddr_in local_address;
    struct sockaddr_in remote_address;
    struct sockaddr_in radio_address;
    socklen_t rcva_len;
    /* Shit Shittington from Shitton Shire */
    char* currentArg;
    char msgData[RADIO_ID_SIZE];

    pthread_t requestThread, interfaceThread;

    /* Reading commandline arguments. Legend:
     *     d - DuISCOVER_ADDR
     *     U - UI_PORT
     *     b - BSIZE
     *     R - RTIME    */
    if((currentArg = extract('d', argc, argv)) != NULL)
        strcpy(DISCOVER_ADDR, currentArg);
    if((currentArg = extract('U', argc, argv)) != NULL)
        UI_PORT = (unsigned short)atoi(currentArg);
    if((currentArg = extract('b', argc, argv)) != NULL)
        BSIZE = (size_t)atol(currentArg);
    if((currentArg = extract('R', argc, argv)) != NULL)
        RTIME = (unsigned short)atoi(currentArg);
    
    #if DEBUG == 1
        fprintf(stderr,"Diag:\n\tDISCOVER_ADDR = %s\n\t"
           "DATA_PORT = %u\n\tCTRL_PORT = %u\n\t"
           "UI_PORT = %u\n\tBSIZE = %zu\n\t"
           "RTIME = %u\n",
            DISCOVER_ADDR, DATA_PORT, CTRL_PORT,
            UI_PORT, BSIZE, RTIME);
    #endif

    /* Not playing anything */
    radioList.playingRadioInd = -1;

    /* opening socket for sending requests and receving radio IDs */
    ctrlSock = socket(AF_INET, SOCK_DGRAM, 0);
    if(ctrlSock < 0)
        syserr("Init: ctrlSock");

    /* activating broadcast */
    optval = 1;
    if(setsockopt(ctrlSock, SOL_SOCKET, SO_BROADCAST, (void *)&optval, 
            sizeof(optval)) < 0)
        syserr("Init: setsockopt broadcast");

    /* setting TTL for datagrams sended to group */
    optval = TTL_VALUE;
    if(setsockopt(ctrlSock, SOL_IP, IP_MULTICAST_TTL, (void *)&optval,
            sizeof(optval)) < 0)
        syserr("Init: setsockopt multicast ttl");

    /* binding to local address and port */
    local_address.sin_family = AF_INET;
    local_address.sin_addr.s_addr = htonl(INADDR_ANY);
    local_address.sin_port = htons(0);
    if(bind(ctrlSock,  (struct sockaddr *)&local_address, sizeof(local_address)) < 0) 
        syserr("Init: bind ctrlSock");

    /* setting broadcast address and port */
    remote_address.sin_family = AF_INET;
    remote_address.sin_port = htons(CTRL_PORT);
    if(inet_aton(DISCOVER_ADDR, &remote_address.sin_addr) == 0)
        syserr("Init: inet_aton DISCOVER_ADDR");

    if(pthread_create(&requestThread, 0, handleEmitting, (void *)&ctrlSock) < 0)
        syserr("Init: creating thread for emitting ID requests");

    if(pthread_create(&interfaceThread, 0, handleInterface, NULL) < 0)
        syserr("Init: creating thread for emitting ID requests");

    memset(msgData, 0, RADIO_ID_SIZE);
    rcva_len = (socklen_t) sizeof(radio_address);
    flags = 0;
    for(;;) {
        if((length = recvfrom(ctrlSock, msgData, RADIO_ID_SIZE, flags,
                (struct sockaddr *)&radio_address, &rcva_len)) < 0)
            syserr("Main thread work: recv");
        if(radioFromStr(msgData, &newRadio) >= 0) {
            /* We receive package with data about radio station */
            radioIndex = seekRadio(&radioList, &newRadio);
            pthread_mutex_lock(&radioListMutex);
            if(radioIndex < 0 ) {
                /* Radio not found on the list, we have to add */
                insertRadio(&radioList, &newRadio, &radio_address);
                /* Signing that radio has changed */
                radioListChanged = _POSIX_OPEN_MAX + 1;
                /* Starting thread responsible for receiving data from radio */
                if(radioList.playingRadioInd < 0) {
                    radioList.playingRadioInd = 0;
                    if(pthread_create(&bufferThread, 0, handleBuffer, NULL) < 0)
                        syserr("Init: creating thread for emitting ID requests");
                } else pthread_mutex_unlock(&radioListMutex);

            } else {
                /* Radio already in, we have to reset the number of
                 * warnings, so we dont accidentaly remove our station */
                radioList.radio[radioIndex].warnings = 0;
                pthread_mutex_unlock(&radioListMutex);
                /* cleaning memory */
                destroyRadioEntry(&newRadio);
            }
            
        }
        memset(msgData, 0, RADIO_ID_SIZE);
    }
    return 0; 
}
