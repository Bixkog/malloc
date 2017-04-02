CC=gcc
CFLAGS= -Wall -Wextra -g -fPIC -std=gnu11 -pthread -D_GNU_SOURCE

all: malloc.o libmem.so test

libmem.so: CFLAGS+= -shared
libmem.so: malloc.o mem_arena.o
	$(CC) $(CFLAGS) -o $@ $^

malloc.o: mem_arena.o

test: malloc.o mem_arena.o

clean:
	$(RM) *.so *.o test core

