#ifndef _ARCHIVER
#define _ARCHIVER

#include<string.h>
#include<stdlib.h>

/* BEWARE! Poorly written macro, use at your own responsibility! */
#define newMessageNumber counter+1

typedef struct {
    size_t nr;
    char* content;
} Package;

/* Archiver is responsible for keeping last packages. 
 * Packages are number from 1 */
typedef struct {
    /* Keep last Archiver.size packages in package */
    Package* packages;
    /* ptr shows current package to change */
    size_t ptr, size;
    /* How many packages have been in archiver */
    size_t psize;
    size_t counter;
} Archiver;


void initArchiver(Archiver* a, size_t n, size_t PSIZE) {
    size_t i;

    a->size = n;
    a->psize = PSIZE;
    a->packages = (Package*) malloc(a->size * sizeof(Package));
    for(i = 0; i < a->size; i++) {
        /*  +1 because strcpy will add '\0' at the end */
        a->packages[i].content = (char*)malloc((PSIZE + 1) * sizeof(char));
        a->packages[i].nr = 0;
    }
    a->ptr = 0;
    a->counter = 0;
}

void destroyArchiver(Archiver* a) {
    size_t i;
    for(i = 0; i < a->size; i++)
        free(a->packages[i].content);
    free(a->packages);
}

Package* getKthPackage(Archiver* a, size_t k) {
    /* Check if there is possibility of having package */
    int shift;
    if(k >= a->counter || k == 0)
        return NULL;
    if(k < a->packages[a->ptr].nr)
        return NULL;
    if(a->counter <= a->size) {
        if(a->counter < k)
            /* We dont have so many packages in archiver */
            return NULL;
        else return &a->packages[k - 1];
    } else { /* Now the beginning of packages is under ptr */
        shift = k - a->packages[a->ptr].nr;
        return &a->packages[(a->ptr + shift) % a->size];
    }
}

void insertPackage(Archiver* a, char* content) {
    memcpy(a->packages[a->ptr].content, content, a->psize);
    a->counter++;
    a->packages[a->ptr].nr = a->counter;
    a->ptr++;
    a->ptr %= a->size;
}

#endif // _ARCHIVER
