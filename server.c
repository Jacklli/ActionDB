/*
* Copyright 2015, Liyinlong.  All rights reserved.
* https://github.com/Jacklli/Action
*
* Use of this source code is governed by GPL v2 license
* that can be found in the License file.
*
* Author: Liyinlong (yinlong.lee at hotmail.com)
*/

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include "buffer.h"
#include "event.h"
#include "aepoll.h"
#include "timer.h"
#include "tcpsocket.h"
#include "conn.h"
#include "rbtree.h"
#include "server.h"
#include "dict.h"
#include "log.h"
#include "threads.h"


int serverport = PORT;
int listenfd = 0;

dict *db[THREADCNT] = {NULL};
eventLoop *globalEloop[THREADCNT] = {NULL};
connTree *globalconnTree[THREADCNT] = {NULL};
pthread_mutex_t lock[THREADCNT];
pthread_mutex_t eloopidLock;
int eloopid = 0;

pthread_mutex_t connLock[THREADCNT];


/* these global variables are exposed to all threads */
/* they are all immutable */
extern dictType dDictType;


int main() {
    int i = 0, j = 0;
    pthread_t tid[THREADCNT+2];

    listenfd = tcpServer(NULL,serverport , NULL);   // init for the global variable listenfd,
                                                    // the first NULL refers to server->neterr,
    writeLog(1,"starting server...%s","server started.");
    logFile("starting server...", 0);
    for(i = 0; i < THREADCNT; i++) {
        db[i] = dictCreate(&dDictType, NULL);           //init global variable db[i]
    }
    for(i = 0;i<THREADCNT;i++) {
        globalEloop[i] = createEventLoop();
        aePollCreate(globalEloop[i]) == -1;
        globalconnTree[i] = (connTree *)malloc(sizeof(connTree));
        globalconnTree[i]->root = NULL;
        globalconnTree[i]->connCnt = 0;    //init connCnt is 0
        globalconnTree[i]->ins = rb_insert;
        globalconnTree[i]->del = rb_erase;
        globalconnTree[i]->get = rb_search;
    }

    pthread_mutex_init(&eloopidLock, NULL);
    pthread_create(&tid[THREADCNT], NULL, acceptThread, NULL);
    for(i = 0; i < THREADCNT; i++) {
        pthread_mutex_init(&lock[i], NULL);
        pthread_create(&tid[i], NULL, runworkThread, NULL);
    }
    pthread_mutex_init(&lock[THREADCNT], NULL);
    pthread_create(&tid[THREADCNT+1], NULL, cronThread, NULL);
    while(j < THREADCNT) {
        pthread_join(tid[j], NULL);
        j++;
    }
    pthread_join(tid[THREADCNT+1], NULL);

    pthread_join(tid[THREADCNT], NULL); //for accept thread
    return 1;
}
