#include build_config.mk
CFLAGS += -g -I ../net/ -I ../db/ -I ../log/ -I ../ -I ../log/ -I ../rocksdb-3.9/include/rocksdb/
test_rbtree.o: ../net/rbtree.h ../net/rbtree.c test_rbtree.c
	gcc $(CFLAGS) -o test_rbtree.o ../net/rbtree.c test_rbtree.c
test_aepolltimer.o: test_aepolltimer.c ../log/log.h ../net/event.h ../net/event.c ../net/aepoll.h ../net/aepoll.c ../net/timer.h ./timer.c
	gcc $(CFLAGS) -o test_aepolltimer.o test_aepolltimer.c ../net/event.c ../net/aepoll.c ./timer.c ../log/log.c
testperf.o: testperf.c
	gcc $(CFLAGS) -g -o testperf.o testperf.c -lpthread
testlog.o: ../log/log.c ../log/log.h testlog.c
	gcc $(CFLAGS) -g -o testlog.o ../log/log.c testlog.c
simple_example: simple_example.cc
	g++  simple_example.cc -osimple_example ../rocksdb-3.9/librocksdb.a -I../include -O2 -std=c++11 -lpthread -lrt -lsnappy -lgflags -lz -lbz2 -std=c++11  -DROCKSDB_PLATFORM_POSIX  -DOS_LINUX -fno-builtin-memcmp -DROCKSDB_FALLOCATE_PRESENT -DSNAPPY -DGFLAGS=google -DZLIB -DBZIP2
clean:
	rm -rf *.o simple_example
