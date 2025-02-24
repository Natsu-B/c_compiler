CC=gcc
CFLAGS=-std=c11 -g -static -Wall
ASFLAGS=-g
SRCS=$(wildcard *.c)
ASRCS=$(wildcard *.s)
OBJS=$(SRCS:.c=.o) $(ASRCS:.s=.o)

build: $(OBJS)

%.o: %.s
	$(CC) $(ASFLAGS) -c $< -o $@

clean:
	rm -f *.o

.PHONY: build clean
