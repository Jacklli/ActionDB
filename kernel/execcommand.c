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
#include <malloc.h>
#include <string.h>
#include "object.h"
#include "execcommand.h"
#include "server.h"
#include "log.h"
#include "ds.h"


int chrtoint(char *str) {
    int size = 0;
    unsigned int intval = 0;
    int i = 0;
    size = strlen(str);
    for(i = 0; i<size; i++) {
        intval += *(str+i)-'0';
    }
    return intval;
}


int execSetCommand(char (*argv)[ARGUMENTCNT]) {
    valObject *val = createObj(argv[2]);
    return rocksSet(argv[1],val->ptr);
}
valObject *execGetCommand(char (*argv)[ARGUMENTCNT]) {
    rocksGet(argv[1]);   
    return NULL;
}

void execShutdownCommand() {
    rocksShutdown();    
}
