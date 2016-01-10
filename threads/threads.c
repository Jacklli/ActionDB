/*
* Copyright 2015, Liyinlong.  All rights reserved.
* https://github.com/Jacklli/action
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
#include "dict.h"
#include "log.h"
#include "threads.h"

Server *initworkThread() {
    int i = 0;
    pthread_attr_t pattr;
    size_t size = 0;

    Server *server = (Server *)malloc(sizeof(struct Server));
    //init server network
    server->neterr = NULL;
    server->port = serverport;  //temp use for 3306
    server->bindaddr = NULL;
    server->listenfd = listenfd;

    //init conection tree, each thread has its own connection ree
    pthread_mutex_lock(&eloopidLock);
    printf("eloopid for current thread is:%d\n",eloopid);
    server->eLoop = globalEloop[eloopid];
    server->tree = globalconnTree[eloopid];
    eloopid++;
    pthread_mutex_unlock(&eloopidLock);

    return server;
}

int mainLoop(Server *server) {
    int j = 0, numevents = 0, processed = 0;
    struct timeval tvp;

/* main loop */
    while(1) {
        tvp.tv_sec = 0;
        tvp.tv_usec = 1;
        numevents = 0;
        numevents = aePollPoll(server->eLoop, &tvp);
        for (j = 0; j < numevents; j++) {
            fileEvent *fe = &server->eLoop->fEvents[server->eLoop->fired[j].fd];
            int mask = server->eLoop->fired[j].mask;
            int fd = server->eLoop->fired[j].fd;

            /* note the fe->mask & mask & ... code: maybe an already processed
             * event removed an element that fired and we still didn't
             * processed, so we check if the event is still valid. */
            if (fe->mask & mask & READABLE)
                fe->rfileProc(server->tree, server->eLoop, fd, mask);
            processed++;
        }
    }
    return 1;
}

void *runworkThread() {
    Server *server = NULL;
    if(!(server = initworkThread())) {
        printf("initworkThread error!\n");
    }
    if(mainLoop(server) != 1);
}

void *cronThread() {
    int i = 0, keys = 0;
    while(1) {
/*
* fill your own logic here.
*/

        usleep(2000000);
    }
}

void *acceptThread() {
    int i = 0;
    size_t size = 0;
    eventLoop *acceptEloop = createEventLoop();

/* only __this__ thread can accept */
    if (createFileEvent(acceptEloop, listenfd, READABLE, tcpAcceptCallback) == -1
        || aePollCreate(acceptEloop) == -1) return NULL;  //aePollCreate(server->eLoop)只需要在这里执行一次，以后add读写事件就不用再次add
    if (aePollAddEvent(acceptEloop, listenfd, READABLE) == -1) {
        printf("add tcpAcceptCallback failed!\n");
        return NULL;
    }

    int j = 0, numevents = 0, processed = 0;
    struct timeval tvp;

/* main loop */
    while(1) {
        tvp.tv_sec = 0;
        tvp.tv_usec = 1;
        numevents = 0;
        numevents = aePollPoll(acceptEloop, &tvp);
        for (j = 0; j < numevents; j++) {
            fileEvent *fe = &acceptEloop->fEvents[acceptEloop->fired[j].fd];
            int mask = acceptEloop->fired[j].mask;
            int fd = acceptEloop->fired[j].fd;

            /* note the fe->mask & mask & ... code: maybe an already processed
             * event removed an element that fired and we still didn't
             * processed, so we check if the event is still valid. */
            if (fe->mask & mask & READABLE)
                fe->rfileProc(NULL, acceptEloop, fd, mask);
            processed++;
        }
    }
}
