#ifndef _RADIOBUFFER
#define _RADIOBUFFER

#include <string.h>
#include <sys/time.h>

#define DEBUG 0


typedef struct {
    size_t nr;
    struct timeval received;
    char* content;
} Chunk;

typedef struct {
    Chunk* chunks;
    /* shows index of the last chunk */
    int ptr;
    /* shows where Buffer starts -> what is the 1st chunk
     * we can ask for */
    int start;
    /* how many chunks in buffer */
    int size;
    /* chunk content size */
    int psize;
    /* How many packages have been in Buffer */
    int counter;
} Buffer;

/* BSIZE - size of Buffer in bytes
   PSIZE - size of one Chunk content */
void initBuffer(Buffer* b, int n, int PSIZE) {
    int i;
    
    b->size = n;
    b->psize = PSIZE;
    b->chunks = (Chunk*) malloc(b->size * sizeof(Chunk));
    for(i = 0; i < b->size; i++) {
        b->chunks[i].content = (char*)malloc((PSIZE + 10) * sizeof(char));
        memset(b->chunks[i].content, 0, PSIZE);
        b->chunks[i].nr = 0;
        b->chunks[i].received.tv_sec  = 0;
        b->chunks[i].received.tv_usec = 0;
    }
    b->ptr = -1;
    b->start = 0;
    b->counter = 0;
    
}

void destroyBuffer(Buffer* b) {
    int i;
    for(i = 0; i < b->size; i++) {
        free(b->chunks[i].content);
    }
    //free(b->chunks);
}

/* Returns 1 if Chunk is from retransmission package, 0 otherwise
 * If 1, the function checkFulfillment should be called after */
int insertChunk(Buffer* b, char* content, int nr, struct timeval rcv_time) {
    /* index to put fresh new Chunk */
    int chunkIndex;
    int lastChunkNr;
    int shift, i;
    int returnValue;

    if(b->ptr == -1) {
        chunkIndex = 0;
        b->ptr = 0;
        b->start = 0;
        returnValue = 0;
    }
    else {
        /* We have to calculate the index :( */
        lastChunkNr = b->chunks[b->ptr].nr;
        shift = nr - (int)lastChunkNr;
        chunkIndex = b->ptr + shift;
        if(shift > 0 && chunkIndex >= 2 * b->size) {
            /* We received package with so far number, that 
             * there is no chance on retransmission packages
             * from before, so we will reset Buffer */
            for(i = 0; i < b->size; i++) {
                b->chunks[i].nr = 0;
            }
            chunkIndex = 0;
            b->ptr = 0;
            b->start = 0;
            returnValue = 0;
        }
        if(shift > 0 && chunkIndex >= b->size 
            && (chunkIndex % b->size >= b->start)) {
                /* There is hope on retransmission */
            chunkIndex %= b->size;
            i = b->start;

            /* Signing chunks as deprecated */
            /* Pushing starting starting position */
            do {
                b->chunks[i++].nr = 0;
                i %= b->size;
            } while(i != chunkIndex);

            b->ptr = chunkIndex;
            returnValue = 0;
        }
        else if(shift < 0) {
            /* Retransmission package */
            /* We dont trust deceiving % from C */
            if(chunkIndex < 0)
                chunkIndex += b->size;
            if(chunkIndex < 0)
                returnValue = -1;
            else returnValue = 1;
        }
        #if DEBUG == 1
        else if(shift == 0) {
            fprintf(stderr, "You just got same package twice, something is wrong.\n");
            returnValue = 0;
        }
        #endif
        else { 
            /* Normal package with data about next chunks */
            chunkIndex %= b->size;
            b->ptr = chunkIndex;
            returnValue = 0; 
        }
    }

    /* Finally setting right chunk */
    if(returnValue >= 0) {
        memcpy(b->chunks[chunkIndex].content, content, b->psize);
        b->chunks[chunkIndex].received = rcv_time;
        b->chunks[chunkIndex].nr = nr;
        b->counter++;
    }

    return returnValue;
}

/* sets content to the content of the first chunk */
/* returns -1 if cannot pop more */
int popChunk(Buffer* b, char* content) {

    if(b->chunks[b->start].nr == 0)
        return -1;
    if(b->counter == 0)
        return -1;
    else {
        memcpy(content, b->chunks[b->start].content, b->psize);
        /* Resetting  arguments */
        b->chunks[b->start].content[0] = '\0';
        b->chunks[b->start].nr = 0;
        b->counter--;
        b->start++;
        b->start %= b->size;
    }
    return 0;
}
#undef DEBUG 


#endif  // _RADIOBUFFER
