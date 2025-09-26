CC=gcc
CFLAGS=-std=c11 -g -Wall -Wextra -Werror -Wno-builtin-declaration-mismatch -Wno-attributes -D_GNU_SOURCE -DSELF_HOST
ASFLAGS=-g
CPPFLAGS=-E -P -D__MYCC__ -DSELF_HOST
AARCH64_CC ?= aarch64-linux-gnu-gcc

SRCS=$(wildcard *.c)
OBJS=$(SRCS:.c=.o)

TARGET=main
# Use a very safe subset for initial self-hosting.
# Keep it tiny and low risk first.
# Start minimal self-hosting with only main.c
SAFE_SRCS=main.c

# Color definitions for echo
CYAN=\033[0;36m
GREEN=\033[0;32m
YELLOW=\033[0;33m
NC=\033[0m

all: build

build:
	./build.sh
	$(MAKE) $(TARGET)

$(TARGET): $(OBJS)
	@echo -e "${GREEN}--> Linking $@${NC}"
	$(CC) $(CFLAGS) -o $@ $(OBJS)

test: all
	@echo -e "${CYAN}--- Running tests on default target ($(TARGET))... ---${NC}"
	./test.sh ./main

.PHONY: test-x86_64 test-aarch64

test-x86_64: all
	@echo -e "${CYAN}--- Running tests (x86_64) ---${NC}"
	./test.sh ./main "" gcc gcc

test-aarch64: all
	@echo -e "${CYAN}--- Building test objects for AArch64 and running tests ---${NC}"
	./test.sh ./main "-target aarch64" $(AARCH64_CC) $(AARCH64_CC)

%.o: %.c
	@echo -e "${CYAN}Compiling $<...${NC}"
	$(CC) $(CFLAGS) -c $< -o $@

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
			$(CC) $(CFLAGS) -S -masm=intel -o out/$$s_file $$src; \
		fi; \
	done
	@echo -e "${CYAN}[3/4] Assembling generated sources...${NC}"
	for src in $(SRCS); do \
		s_file=$${src%.c}.s; \
		o_file=$${src%.c}.o; \
		$(CC) $(ASFLAGS) -c out/$$s_file -o out/$$o_file; \
	done
	@echo -e "${CYAN}[4/4] Linking to create $(TARGET)2...${NC}"
	$(CC) $(CFLAGS) -o $(TARGET)2 out/*.o
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

self1-test: test self1
	@echo -e "\n${CYAN}--- Running tests after self-host... ---${NC}"
	./test.sh ./main2

# Minimal self-host: only compile a safe subset (e.g., main.c) with ./main
self1-min: $(TARGET)
	@echo -e "${CYAN}--- Starting minimal self-host (subset: $(SAFE_SRCS)) ---${NC}"
	rm -f .success_list .failure_list
	mkdir -p out
	@echo -e "${CYAN}[1/4] Preprocessing source files...${NC}"
	for src in $(SRCS); do \
		$(CC) $(CPPFLAGS) $$src -o out/$$src; \
	done
	@echo -e "${CYAN}[2/4] Compiling subset with ./$(TARGET); others with gcc...${NC}"
	for src in $(SRCS); do \
		s_file=$${src%.c}.s; \
		if echo " $(SAFE_SRCS) " | grep -q " $$src "; then \
			if ./$(TARGET) -i out/$$src -o out/$$s_file; then \
				echo -e "    [${GREEN}SELF${NC}] Compiled '$${src}' with ./main."; \
				echo "$$src" >> .success_list; \
			else \
				echo -e "    [${YELLOW}FALLBACK${NC}] Failed on '$${src}'. Using gcc."; \
				echo "$$src" >> .failure_list; \
				$(CC) $(CFLAGS) -S -masm=intel -o out/$$s_file $$src; \
			fi; \
		else \
			$(CC) $(CFLAGS) -S -masm=intel -o out/$$s_file $$src; \
		fi; \
	done
	@echo -e "${CYAN}[3/4] Assembling generated sources...${NC}"
	for src in $(SRCS); do \
		s_file=$${src%.c}.s; \
		o_file=$${src%.c}.o; \
		$(CC) $(ASFLAGS) -c out/$$s_file -o out/$$o_file; \
	done
	@echo -e "${CYAN}[4/4] Linking to create $(TARGET)2...${NC}"
	$(CC) $(CFLAGS) -o $(TARGET)2 out/*.o
	@echo ""
	@echo -e "${CYAN}--- Minimal Self-Host Summary ---${NC}"
	@if [ -f .success_list ]; then \
		echo -e "${GREEN}Compiled with ./main:${NC}"; \
		cat .success_list | sed 's/^/  - /'; \
	fi
	@if [ -f .failure_list ]; then \
		echo -e "\n${YELLOW}Compiled with gcc:${NC}"; \
		cat .failure_list | sed 's/^/  - /'; \
	fi
	@echo ""
	@echo -e "${GREEN}--- Minimal self-host complete. Executable '$(TARGET)2' created. ---${NC}"

self1-min-test: test self1-min
	@echo -e "\n${CYAN}--- Running tests after minimal self-host... ---${NC}"
	./test.sh ./main2

clean:
	rm -f *.o $(TARGET) $(TARGET)2
	rm -f .success_list .failure_list
	rm -rf out

.PHONY: all clean self1 test self1-test build self1-min self1-min-test
