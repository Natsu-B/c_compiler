CC=gcc
CFLAGS=-std=c11 -g -static -Wall
SRCS=$(wildcard *.c)
OBJS=$(SRCS:.c=.o)

main: $(OBJS)
		$(CC) -o main $(OBJS) $(LDFLAGS)

test: main
	./test.sh

clean:
	rm -f main *.o *~ tmp*

.PHONY: test clean
