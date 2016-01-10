/*
* Copyright 2015, Liyinlong.  All rights reserved.
* https://github.com/Jacklli/Action
*
* Use of this source code is governed by GPL v2 license
* that can be found in the License file.
*
* Author: Liyinlong (yinlong.lee at hotmail.com)
*/

#ifndef __SERVER_H__
#define __SERVER_H__

#include <pthread.h>
#include "event.h"
#include "buffer.h"
#include "conn.h"
#include "dict.h"
#include "object.h"

/* refers to your CPU count */
#define THREADCNT 15
#define PORT      33060
#define READABLE 1


extern int serverport;
extern int listenfd;

extern dict *db[THREADCNT];
extern eventLoop *globalEloop[THREADCNT];
extern connTree *globalconnTree[THREADCNT];
extern pthread_mutex_t lock[THREADCNT];
extern pthread_mutex_t eloopidLock;
extern int eloopid;

extern pthread_mutex_t connLock[THREADCNT];

typedef struct Server {
    int listenfd;
    int port;
    char *bindaddr;
    char *neterr;
    int tcp_backlog;
    connTree *tree;
    eventLoop *eLoop;
} Server;

int initServer(Server *server);

#endif
