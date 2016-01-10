#include <iostream>
#inlcude "rocksdb-1.5.7/db.h"


int main() {
    rocksdb::DB* db;
    rocksdb::Options options;
    options.create_if_missing = true;
    rocksdb::Status status =
      rocksdb::DB::Open(options, "/tmp/testdb", &db);
}
