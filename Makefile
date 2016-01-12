CFLAGS += -g -I ./net/ -I ./kernel/ -I ./ -I ./log/ -I ./threads/ -I ./rocksdb-3.9/include/
HEADER = ./net/event.h ./net/aepoll.h ./net/tcpsocket.h \
        ./net/conn.h ./net/timer.h ./net/buffer.h ./net/rbtree.h ./net/codec.h ./kernel/object.h ./kernel/execcommand.h ./kernel/ds.h ./log/log.h ./server.h
BODY = server.cc ./net/event.c ./net/aepoll.c ./net/tcpsocket.c \
        ./net/conn.c ./net/timer.c ./net/buffer.c ./net/rbtree.c ./net/codec.c ./kernel/object.c ./kernel/execcommand.c ./kernel/ds.cc ./log/log.c ./threads/threads.c
#server: ./net/event.o ./net/aepoll.o ./net/tcpsocket.o ./net/conn.o ./net/timer.o ./net/codec.o ./kernel/dict.o ./kernel/object.o ./kernel/execcommand.o ./log/log.o ./threads/threads.o server.c server.h
	#gcc $(CFLAGS) -g -o server $(BODY) -lpthread
server: $(HEADER) $(BODY) 
	g++ $(CFLAGS) -g  -o server $(BODY) ./rocksdb-3.9/librocksdb.a -I../include -lpthread -std=c++11 -lrt -lsnappy -lgflags -lz -lbz2 -DROCKSDB_PLATFORM_POSIX  -DOS_LINUX -fno-builtin-memcmp -DROCKSDB_FALLOCATE_PRESENT -DSNAPPY -DGFLAGS=google -DZLIB -DBZIP2 -fpermissive -w 
clean:
	rm -rf *.o net/*.o kernel/*.o log/*.o threads/*.o *.log core* server.log.* server tests/*.o tests/testperfget/*.o tests/testperfset/*.o
corev:
	rm -rf core*
