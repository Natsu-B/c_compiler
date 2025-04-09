CC=gcc
CFLAGS=-std=c11 -g -static -Wall -Wextra -Werror

SRCS=$(filter-out preprocessor.c conditional_inclusion.c, $(wildcard *.c))

PREPROCESS_SRCS=$(filter-out main.c, $(wildcard *.c))

ifeq ($(MAKECMDGOALS), preprocess)
    OBJS=$(PREPROCESS_SRCS:.c=.o)
	CFLAGS+= -DPREPROCESS
else
    OBJS=$(SRCS:.c=.o)
endif

main: $(OBJS)
	$(CC) -o main $(OBJS) $(LDFLAGS)

test: main
	./test.sh

clean:
	rm -f main *.o *~ tmp*

preprocess: main

.PHONY: test clean preprocess
