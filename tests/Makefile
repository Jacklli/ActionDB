CFLAGS += -g -I ../net/ -I ../db/ -I ../log/ -I ../ -I ../log/ -I ../rocksdb-1.5.7/
test_rbtree.o: ../net/rbtree.h ../net/rbtree.c test_rbtree.c
	gcc $(CFLAGS) -o test_rbtree.o ../net/rbtree.c test_rbtree.c
test_aepolltimer.o: test_aepolltimer.c ../log/log.h ../net/event.h ../net/event.c ../net/aepoll.h ../net/aepoll.c ../net/timer.h ./timer.c
	gcc $(CFLAGS) -o test_aepolltimer.o test_aepolltimer.c ../net/event.c ../net/aepoll.c ./timer.c ../log/log.c
testperf.o: testperf.c
	gcc $(CFLAGS) -g -o testperf.o testperf.c -lpthread
testlog.o: ../log/log.c ../log/log.h testlog.c
	gcc $(CFLAGS) -g -o testlog.o ../log/log.c testlog.c
testorckdb.o: testrocksdb.cc
	g++ $(CFLAGS) -g -o testorckdb.o testrocksdb.cc
clean:
	rm -rf *.o
