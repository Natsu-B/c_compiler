CC=gcc
CFLAGS=-std=c11 -g -static -Wall -Wextra -Werror

SRCS=$(wildcard *.c)
OBJS=$(SRCS:.c=.o)

main: $(OBJS)
	$(CC) -o main $(OBJS)

test: main
	./test.sh

clean:
	rm -f main *.o *~ tmp*

.PHONY: test clean