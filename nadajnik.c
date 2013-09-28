#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>

#include "err.h"
#include "common.h"
#include "queue.h"
#include "archiver.h"

#define DEBUG 0

#define TTL_VALUE 5
/* max size of messages received - ID requests and retransmission requests */
#define RCV_BUFFER_SIZE 200
/* length of IPv3 address in dotted form with reserve */
#define IPv4_DOTTED_SIZE 17

/* used to block thread in parallely sending datagrams */
static pthread_mutex_t sendMutex = PTHREAD_MUTEX_INITIALIZER;
/* used when copying state of fifoBuffer */
static pthread_mutex_t archiveMutex = PTHREAD_MUTEX_INITIALIZER;
/* used when clearing queue of clients waiting on retransmission */
static pthread_mutex_t queueMutex = PTHREAD_MUTEX_INITIALIZER;

char* MCAST_ADDR = NULL;
size_t PSIZE   = 512;
size_t FSIZE   = 128 * 1024;
char* NAME  = "Nienazwany Nadajnik";
const char VERSION[] = "1.0";
/* Maximum length of package number */


Queue waitingOnRetransmit;
Archiver archive;


/* Thread responsible for handling control messages:
 *  - ID requests (thread is sending respones)
 *  - retrasmit package requests (other thread responds) */
void* handleControl(void* argv0) {
    int ctrlSock;
    int flags, missingPackageNr;
    struct sockaddr_in local_address;
    struct sockaddr_in client_address;
    
    socklen_t rcva_len, snda_len;
    ssize_t length, snd_len;
    char buffer[RCV_BUFFER_SIZE];
    char dummy[RCV_BUFFER_SIZE];
    char* programName;

    #if DEBUG == 1
        char clientIP[IPv4_DOTTED_SIZE];
        printf("Diag:\n\tStarting thread responsible for RECEIVING REQUESTS.\n"
               "\tThread number = %lu, PID = %d\n", (unsigned long)pthread_self(), getpid());
    #endif

    programName = (char*) argv0;

    /* opening socket for requests */
    ctrlSock = socket(AF_INET, SOCK_DGRAM, 0);
    if(ctrlSock < 0)
        syserr("Init: ctrlSock");

    /* binding to local address and port */
    local_address.sin_family = AF_INET;
    local_address.sin_addr.s_addr = htonl(INADDR_ANY);
    local_address.sin_port = htons(CTRL_PORT);

    if(bind(ctrlSock, (struct sockaddr*) &local_address,
                (socklen_t) sizeof(local_address)) < 0)
        syserr("Init: bind ctrlSock");

    rcva_len = (socklen_t) sizeof(local_address);
    snda_len = (socklen_t) sizeof(client_address);
    for(;;) {
        do {
            flags = 0; // nothing special I guess
            length = recvfrom(ctrlSock, buffer, sizeof(buffer), flags,
                    (struct sockaddr *) &client_address, &rcva_len);
            if(length < 0)
                syserr("CtrlWork: error on datagram from client");
            #if DEBUG == 1
                inet_ntop(AF_INET, &(client_address.sin_addr), clientIP, INET_ADDRSTRLEN);
                printf("Diag:\n\tClient address: %s, port: %d\n", clientIP,
                        ntohs(client_address.sin_port));
                printf("Read from ctrlSock:[%s]\n", buffer);
            #endif
            if(strncmp(buffer, ID_REQUEST, strlen(ID_REQUEST)) == 0) {
                /* Message back should contains: program name, 
                 * broadcasting address, version and radio name 
                 * Assumption: There are no "" in program name and radio name */
                memset(buffer, 0, RCV_BUFFER_SIZE);
                sprintf(buffer, "%s\" %s v:%s \"%s", programName, MCAST_ADDR, VERSION, NAME);
                length = (ssize_t)strlen(buffer);
                flags = 0;
                #if DEBUG == 1
                    printf("Diag:\n\t Sending back message: [%s]\n", buffer);
                #endif
                snd_len = sendto(ctrlSock, buffer, length, flags, (struct sockaddr*) &client_address, snda_len);
                if(snd_len != length)
                    syserr("Control Thread Work: error on sending ID message back");

            } else if(strncmp(buffer, RETRANSMIT_REQUEST, 
                        strlen(RETRANSMIT_REQUEST)) == 0) {
                //inet_ntop(AF_INET, &(client_address.sin_addr), clientIP, INET_ADDRSTRLEN);
                sscanf(buffer, "%s %d", dummy, &missingPackageNr);
                pthread_mutex_lock(&queueMutex);
                push_back(&waitingOnRetransmit, missingPackageNr, client_address);
                pthread_mutex_unlock(&queueMutex);
            } else {
                #if DEBUG == 1
                    printf("You shouldn't be here... Anyway, message"
                           "you recevied is:\n [%s]\n", buffer);
                #endif
            }
            

            memset(buffer, 0, RCV_BUFFER_SIZE);
        } while(length > 0);

    }
}

/* We have to assure that msgData could contain content and nr */
void makeMessage(char* msgData, size_t nr, char* content) {
    char temp[MAX_PACKAGE_NR_LENGTH];

    sprintf(temp, "%*zu ", MAX_PACKAGE_NR_LENGTH, nr);
    memcpy(msgData, temp, MAX_PACKAGE_NR_LENGTH + 1);
    memcpy(msgData + MAX_PACKAGE_NR_LENGTH + 1, content, PSIZE);
}

/* Thread responsible for sending packages again to receivers */
void* handleRetransmission(void* dataSockPtr) {

    int dataSock;
    int length, flags;
    char* msgData;
    List currentClient;
    Package* package;
    Queue clients;
    #if DEBUG == 1
        char clientIP[IPv4_DOTTED_SIZE];
    #endif

    struct sockaddr_in client_address;
    socklen_t snda_len;

    dataSock = *((int*)dataSockPtr);

    #if DEBUG == 1
        printf("Diag:\n\tStarting thread responsible for PACKAGE RETRASMISSION.\n"
               "\tThread number = %lu, PID = %d\n", (unsigned long)pthread_self(), getpid());
    #endif

    msgData = (char*) malloc(MAX_PACKAGE_NR_LENGTH + PSIZE);
    memset(msgData, 0, MAX_PACKAGE_NR_LENGTH + PSIZE);

    snda_len = (socklen_t) sizeof(client_address);
    for(;;) {
        if(waitingOnRetransmit.front != NULL) {
            /* Copying queue of waiting clients, so that
             * it can happily add new clients after */
            pthread_mutex_lock(&queueMutex);
            copyAndDestroyQueue(&clients, &waitingOnRetransmit); 
            pthread_mutex_unlock(&queueMutex);

            while(clients.front != NULL) {
                currentClient = *clients.front;
                package = getKthPackage(&archive, currentClient.package);
                if(package != NULL) {
                    makeMessage(msgData, package->nr, package->content);
                    client_address = currentClient.address;
                    length = strlen(msgData);
                    flags = 0;
                    #if DEBUG == 1
                        inet_ntop(AF_INET, &(client_address.sin_addr), clientIP, INET_ADDRSTRLEN);
                        printf("Diag:\n\tSending retransmission package nr: %zu to %s\n",
                                currentClient.package, clientIP);
                    #endif
                    
                    if(sendto(dataSock, msgData, length, flags, 
                            (struct sockaddr *) &client_address, snda_len) != length)
                        syserr("Retransmission thread work: sending datagram to client");
                    memset(msgData, 0, MAX_PACKAGE_NR_LENGTH + PSIZE);
                }
                pop_front(&clients);
            }
        }
        /* sleep RTIME in miliseconds */
        /* Ad. I realize that it might not be the most precise implementation,
         * since sending to clients may take some time and we may finish the
         * loop iteration in slightly more time, but the simplicity of this solution
         * wins with this small diffrence in timing. */
        usleep(RTIME * 1000);
    }
    return NULL;
}


int main(int argc, char* argv[]) {
    size_t length;
    char* currentArg, *newContent;
    char* msgData;
    
    int flags;
    socklen_t snda_len;
    
    /* variables describing sockets */
    int dataSock, optval;
    struct sockaddr_in local_address;
    struct sockaddr_in remote_address;

    pthread_t ctrlThread, retransThread;

    if(argc < 3)
        fatal("Init: Too few arguments, setting -a $(MCAST_ADDR) is mandatory\n");

    /* Reading commandline arguments. Legend:
     *     a - MCAST_ADDR
     *     P - DATA_PORT
     *     C - CTRL_PORT
     *     p - PSIZE
     *     f - FSIZE
     *     R - RTIME
     *     n - NAME     */
    if((MCAST_ADDR = extract('a', argc, argv)) == NULL)
        fatal("Init: Setting -a $(MCAST_ADDR) is mandatory\n");
    if((currentArg = extract('P', argc, argv)) != NULL)
        DATA_PORT = (unsigned short)atoi(currentArg);
    if((currentArg = extract('C', argc, argv)) != NULL)
        CTRL_PORT = (unsigned short)atoi(currentArg);
    if((currentArg = extract('p', argc, argv)) != NULL)
        PSIZE = (size_t)atoi(currentArg);
    if((currentArg = extract('f', argc, argv)) != NULL)
        FSIZE = (size_t)atoi(currentArg);
    if((currentArg = extract('R', argc, argv)) != NULL)
        RTIME = (unsigned short)atoi(currentArg);
    if((currentArg = extract('n', argc, argv)) != NULL)
        NAME = currentArg;
    #if DEBUG
        printf("Diag:\n\tMCAST_ADDR = %s\n\t"
               "DATA_PORT = %u\n\tCTRL_PORT = %u\n\t"
               "PSIZE = %zu\n\tFSIZE = %zu\n\t"
               "RTIME = %u\n\tNAME = %s\n",
                MCAST_ADDR, DATA_PORT, CTRL_PORT,
                PSIZE, FSIZE, RTIME, NAME);
    #endif

    /* opening socket for sending data  */
    dataSock = socket(AF_INET, SOCK_DGRAM, 0);
    if(dataSock < 0)
        syserr("Init: dataSock");

    /* activating broadcast */
    optval = 1;
    if(setsockopt(dataSock, SOL_SOCKET, SO_BROADCAST, (void*)&optval, sizeof(optval)) < 0)
        syserr("Init: setsockopt broadcast");

    /* setting TTL for datagrams sended to group */
    optval = TTL_VALUE;
    if(setsockopt(dataSock, SOL_IP, IP_MULTICAST_TTL, (void*)&optval, sizeof(optval)) < 0)
        syserr("Init: setsockopt multicast ttl");

    /* binding to local address and port */
    local_address.sin_family = AF_INET;
    local_address.sin_addr.s_addr = htonl(INADDR_ANY);
    local_address.sin_port = htons(0);
    if(bind(dataSock, (struct sockaddr *)&local_address, sizeof(local_address)) < 0)
        syserr("Init: bind dataSock");

    /* setting broadcast address and port*/
    remote_address.sin_family = AF_INET;
    remote_address.sin_port = htons(DATA_PORT);
    if(inet_aton(MCAST_ADDR, &remote_address.sin_addr) == 0)
        syserr("Init: inet_aton MCAST_ADDR");
    /*
    if(connect(dataSock, (struct sockaddr *)&remote_address, sizeof(remote_address)) < 0)
        syserr("Init: connect remote_address");
    */

    /* creating separate thread for getting requests on CTRL_PORT */
    if(pthread_create(&ctrlThread, 0, handleControl, (void*)argv[0]) < 0)
        syserr("Init: creating thread for getting requests");

    /* creating separate thread for sending back lost packages */
    if(pthread_create(&retransThread, 0, handleRetransmission, (void*)&dataSock) < 0)
        syserr("Init: creating thread for retransmission");

    /* Preparing archive to work */
    /* FSIZE / PSIZE is number of packages which we keep in history */
    initArchiver(&archive, (size_t)(FSIZE / PSIZE), PSIZE);
    /* +1 because we put \0 at the end, so sprintf will work */
    newContent = (char*) malloc((PSIZE + 1) * sizeof(char));
    memset(newContent, 0, PSIZE + 1);

    /* Preparing msgData */
    msgData = (char*) malloc(MAX_PACKAGE_NR_LENGTH + PSIZE);
    snda_len = (socklen_t) sizeof(remote_address);
    
    for(;;) {
        length = fread(newContent, 1, PSIZE, stdin);
        if(length == 0)
            break;
        #if DEBUG == 1
            /* Because this messages are big we put them on syserr */
            /*
            fprintf(stderr, "Diag:\n\tpackage_nr: %zu\n\tMessage: [%s]\n",
                    archive.newMessageNumber, newContent);
                    */
        #endif
        /* New package is complete, now we have to send it... */
        //sprintf(msgData, "%.20zu %s", archive.newMessageNumber, newContent);
        makeMessage(msgData, archive.newMessageNumber, newContent);
        pthread_mutex_lock(&sendMutex);
        flags = 0;
        if(sendto(dataSock, msgData, MAX_PACKAGE_NR_LENGTH + PSIZE, flags,
                (struct sockaddr *)&remote_address, snda_len) != MAX_PACKAGE_NR_LENGTH + PSIZE) {
            pthread_mutex_unlock(&sendMutex);
            syserr("Main thread work: write");
        }
        pthread_mutex_unlock(&sendMutex);
        /* ... and insert into archive */
        pthread_mutex_lock(&archiveMutex);
        insertPackage(&archive, newContent);
        pthread_mutex_unlock(&archiveMutex);
        /* Clearing rubbish, just to be sure */
        memset(msgData, 0, MAX_PACKAGE_NR_LENGTH + PSIZE);
        memset(newContent, 0, PSIZE);
    }
    free(newContent);
    free(msgData);
    destroyArchiver(&archive);
    return 0;
}
