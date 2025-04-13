CC=gcc
CFLAGS=-std=c11 -ffunction-sections -fdata-sections -g -static -Wall -Wextra -Werror
LDFLAGS=-Wl,--gc-sections,--print-gc-sections

SRCS=$(wildcard *.c)
OBJS=$(SRCS:.c=.o)

main: $(OBJS)
	$(CC) -o main $(OBJS) $(LDFLAGS)

test: main
	./test.sh

clean:
	rm -f main *.o *~ tmp*

.PHONY: test clean