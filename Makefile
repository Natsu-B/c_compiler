CC=gcc
CFLAGS=-std=c11 -g -static -Wall -Wextra -Werror -Wno-builtin-declaration-mismatch -D_GNU_SOURCE -DSELF_HOST
ASFLAGS=-g
CPPFLAGS=-E -P -D__MYCC__ -DSELF_HOST

SRCS=$(wildcard *.c)
ASRCS=$(wildcard *.s)
OBJS=$(SRCS:.c=.o) $(ASRCS:.s=.o)

TARGET=main

# Color definitions for echo
CYAN=\033[0;36m
GREEN=\033[0;32m
YELLOW=\033[0;33m
NC=\033[0m

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

test: all
	./test.sh

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.s
	$(CC) $(ASFLAGS) -c $< -o $@

self1: $(TARGET)
	@echo -e "${CYAN}--- Starting self-host ---${NC}"
	rm -f .success_list .failure_list
	mkdir -p out
	@echo -e "${CYAN}[1/4] Preprocessing source files...${NC}"
	for src in $(SRCS); do \
		$(CC) $(CPPFLAGS) $$src -o out/$$src; \
	done
	@echo -e "${CYAN}[2/4] Compiling with ./$(TARGET) (fallback to gcc on failure)...${NC}"
	for src in $(SRCS); do \
		s_file=$${src%.c}.s; \
		if ./$(TARGET) -i out/$$src -o out/$$s_file; then \
			echo -e "    [${GREEN}SUCCESS${NC}] Compiled '$${src}' with ./main."; \
			echo "$$src" >> .success_list; \
		else \
			echo -e "    [${YELLOW}WARNING${NC}] Failed on '$${src}'. Falling back to gcc."; \
			echo "$$src" >> .failure_list; \
			$(CC) $(CFLAGS) -S -o out/$$s_file $$src; \
		fi; \
	done
	@echo -e "${CYAN}[3/4] Linking to create $(TARGET)2...${NC}"
	$(CC) $(CFLAGS) -z noexecstack -o $(TARGET)2 out/*.s $(ASRCS)
	@echo -e "${CYAN}[4/4] Finalizing and showing summary...${NC}"
	@echo ""
	@echo -e "${CYAN}--- Self-Host Compilation Summary ---${NC}"
	@if [ -f .success_list ]; then \
		echo -e "${GREEN}Successfully compiled with ./main:${NC}"; \
		cat .success_list | sed 's/^/  - /'; \
	fi
	@if [ -f .failure_list ]; then \
		echo -e "\n${YELLOW}Failed with ./main (Fell back to gcc):${NC}"; \
		cat .failure_list | sed 's/^/  - /'; \
	fi
	@echo ""
	@echo -e "${GREEN}--- Self-host complete. Executable '$(TARGET)2' created. ---${NC}"

clean:
	rm -f *.o $(TARGET) $(TARGET)2
	rm -f .success_list .failure_list
	rm -rf out

.PHONY: all clean self1 test