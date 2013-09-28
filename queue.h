#ifndef _QUEUE
#define _QUEUE

#include<stdlib.h>
#include<netinet/in.h>

struct _List {
    size_t package;
    struct sockaddr_in address;
    struct _List* next;
};

typedef struct _List List;


typedef struct {
    List* back;
    List* front;
} Queue;

inline void push_back(Queue* q, size_t package, struct sockaddr_in address) {
    if(q->back == NULL) {
        q->back = (List*) malloc(sizeof(List));
       q->front = q->back;
    } else {
        q->back->next = (List*) malloc(sizeof(List));
        q->back = q->back->next;
    }
    q->back->package = package;
    q->back->address= address;
    q->back->next = NULL;
}

inline void pop_front(Queue* q){
    if(q->front == NULL)
        return;
    List* temp = q->front;
    q->front = q->front->next;
    free(temp);
}

inline void copyAndDestroyQueue(Queue* dest, Queue* from) {
    dest->front = from->front;
    dest->back = from->back;
    from->front = NULL;
    from->back = NULL;
}



#endif // _QUEUE
