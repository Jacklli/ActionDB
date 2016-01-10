CFLAGS += -g -I ./net/ -I ./kernel/ -I ./ -I ./log/ -I ./threads/
HEADER = ./net/event.h ./net/aepoll.h ./net/tcpsocket.h \
        ./net/conn.h ./net/timer.h ./net/buffer.h ./net/rbtree.h ./net/codec.h ./kernel/object.h ./kernel/dict.h ./kernel/execcommand.h ./log/log.h ./server.h
BODY = server.c ./net/event.c ./net/aepoll.c ./net/tcpsocket.c \
        ./net/conn.c ./net/timer.c ./net/buffer.c ./net/rbtree.c ./net/codec.c ./kernel/dict.c ./kernel/object.c ./kernel/execcommand.c ./log/log.c ./threads/threads.c
server: ./net/event.o ./net/aepoll.o ./net/tcpsocket.o ./net/conn.o ./net/timer.o ./net/codec.o ./kernel/dict.o ./kernel/object.o ./kernel/execcommand.o ./log/log.o ./threads/threads.o server.c server.h
	gcc $(CFLAGS) -g -o server $(BODY) -lpthread
clean:
	rm -rf *.o net/*.o kernel/*.o log/*.o threads/*.o *.log core* server.log.* server
corev:
	rm -rf core*
