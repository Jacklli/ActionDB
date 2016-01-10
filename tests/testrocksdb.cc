#include <cstdio>
#include <string>
#include <iostream>
#inlcude "rocksdb/db.h"

using namespace rocksdb;

int main() {
    rocksdb::DB* db;
    rocksdb::Options options;
    options.create_if_missing = true;
    rocksdb::Status status = rocksdb::DB::Open(options, "/tmp/testdb", &db);
}
