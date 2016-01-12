/*
* Copyright 2015, Liyinlong.  All rights reserved.
* https://github.com/Jacklli/ActionDB
*
* Use of this source code is governed by GPL v2 license
* that can be found in the License file.
*
* Author: Liyinlong (yinlong.lee at hotmail.com)
*/

#include <stdlib.h>
#include <pthread.h>
#include <cstdio>
#include <string>
#include <iostream>
#include "rocksdb/db.h"
#include "rocksdb/slice.h"
#include "rocksdb/options.h"
#include "object.h"
#include "server.h"

using namespace std;
using namespace rocksdb;

extern DB* db;

int rocksSet(char *key, char *val) {
    Status s;
    s = db->Put(WriteOptions(), key, val);
    assert(s.ok());
    std::string value;
    s = db->Get(ReadOptions(), key, &value);
    assert(s.ok());
    cout<<"key is:"<<key<<endl;
    cout<<"value is:"<<value<<endl;
    return 1;
}

int rocksGet(char *key) {
    Status s;
    std::string value;
    s = db->Get(ReadOptions(), key, &value);
    assert(s.ok());
    cout<<"value is:"<<value<<endl;
    return 1;
}

void rocksShutdown() {
    int i = 0;
    delete db;
    for(i = 0; i< THREADCNT; i++) {
        freeConns(globalconnTree[i]);   
    }
    cout<<"rocksDB shutdown success!"<<endl;
    exit(1);
}
