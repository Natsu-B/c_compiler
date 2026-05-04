CC=gcc
CFLAGS=-std=c11 -g -Wall -Wextra -Werror -Wno-builtin-declaration-mismatch -Wno-attributes -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-parameter -D_GNU_SOURCE -DSELF_HOST
LDFLAGS=-no-pie
ASFLAGS=-g
SELF_ASFLAGS ?=
CPPFLAGS=-E -P -D__MYCC__ -DSELF_HOST
AARCH64_CC ?= aarch64-linux-gnu-gcc

SRCS=$(wildcard *.c)
OBJS=$(SRCS:.c=.o)
SELF_OUT ?= .selfhost-out
SELF_OBJS=$(addprefix $(SELF_OUT)/,$(OBJS))
SELF2_OUT ?= .selfhost2-out
SELF2_OBJS=$(addprefix $(SELF2_OUT)/,$(OBJS))
SELF3_OUT ?= .selfhost3-out
SELF3_OBJS=$(addprefix $(SELF3_OUT)/,$(OBJS))
GENERATED_HEADERS=include/include_paths.h include/gcc_predef.h

TARGET=main
# Use a very safe subset for initial self-hosting.
# Keep it tiny and low risk first.
# Start minimal self-hosting with only main.c
SAFE_SRCS=main.c
FORCED_FALLBACK_SRCS=

# Color definitions for echo
CYAN=\033[0;36m
GREEN=\033[0;32m
YELLOW=\033[0;33m
NC=\033[0m

all: build

build:
	./build.sh
	$(MAKE) $(TARGET)

$(GENERATED_HEADERS): build.sh
	./build.sh

$(TARGET): $(GENERATED_HEADERS) $(OBJS)
	@echo -e "${GREEN}--> Linking $@${NC}"
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS)

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

%.o: %.c $(GENERATED_HEADERS)
	@echo -e "${CYAN}Compiling $<...${NC}"
	$(CC) $(CFLAGS) -c $< -o $@

self1: build
	@echo -e "${CYAN}--- Starting self-host ---${NC}"
	rm -f .success_list .failure_list
	mkdir -p $(SELF_OUT)
	@echo -e "${CYAN}[1/4] Preprocessing source files...${NC}"
	for src in $(SRCS); do \
		$(CC) $(CPPFLAGS) $$src -o $(SELF_OUT)/$$src; \
	done
	@echo -e "${CYAN}[2/4] Compiling with ./$(TARGET) (fallback to gcc on failure)...${NC}"
	for src in $(SRCS); do \
		s_file=$${src%.c}.s; \
		if echo " $(FORCED_FALLBACK_SRCS) " | grep -q " $$src "; then \
			echo -e "    [${YELLOW}FORCED${NC}] '$${src}' is pinned to gcc fallback."; \
			echo "$$src" >> .failure_list; \
			$(CC) $(CFLAGS) -S -masm=intel -o $(SELF_OUT)/$$s_file $$src; \
		elif ./$(TARGET) -i $(SELF_OUT)/$$src -o $(SELF_OUT)/$$s_file; then \
			echo -e "    [${GREEN}SUCCESS${NC}] Compiled '$${src}' with ./main."; \
			echo "$$src" >> .success_list; \
		else \
			echo -e "    [${YELLOW}WARNING${NC}] Failed on '$${src}'. Falling back to gcc."; \
			echo "$$src" >> .failure_list; \
			$(CC) $(CFLAGS) -S -masm=intel -o $(SELF_OUT)/$$s_file $$src; \
		fi; \
	done
	@echo -e "${CYAN}[3/4] Assembling generated sources...${NC}"
	for src in $(SRCS); do \
		s_file=$${src%.c}.s; \
		o_file=$${src%.c}.o; \
		$(CC) $(SELF_ASFLAGS) -c $(SELF_OUT)/$$s_file -o $(SELF_OUT)/$$o_file; \
	done
	@echo -e "${CYAN}[4/4] Linking to create $(TARGET)2...${NC}"
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(TARGET)2 $(SELF_OBJS)
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

self1-smoke: self1
	@echo -e "\n${CYAN}--- Running self-host smoke tests... ---${NC}"
	sh ./test/selfhost_smoke.sh ./main2

self2: self1
	@echo -e "${CYAN}--- Starting second-generation self-host ---${NC}"
	rm -f .success_list2 .failure_list2
	mkdir -p $(SELF2_OUT)
	@echo -e "${CYAN}[1/4] Preprocessing source files...${NC}"
	for src in $(SRCS); do \
		$(CC) $(CPPFLAGS) $$src -o $(SELF2_OUT)/$$src; \
	done
	@echo -e "${CYAN}[2/4] Compiling with ./$(TARGET)2 (no fallback)...${NC}"
	for src in $(SRCS); do \
		s_file=$${src%.c}.s; \
		if ./$(TARGET)2 -i $(SELF2_OUT)/$$src -o $(SELF2_OUT)/$$s_file; then \
			echo -e "    [${GREEN}SUCCESS${NC}] Compiled '$${src}' with ./main2."; \
			echo "$$src" >> .success_list2; \
		else \
			echo -e "    [${YELLOW}FAIL${NC}] Failed on '$${src}' with ./main2."; \
			echo "$$src" >> .failure_list2; \
			exit 1; \
		fi; \
	done
	@echo -e "${CYAN}[3/4] Assembling generated sources...${NC}"
	for src in $(SRCS); do \
		s_file=$${src%.c}.s; \
		o_file=$${src%.c}.o; \
		$(CC) $(SELF_ASFLAGS) -c $(SELF2_OUT)/$$s_file -o $(SELF2_OUT)/$$o_file; \
	done
	@echo -e "${CYAN}[4/4] Linking to create $(TARGET)3...${NC}"
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(TARGET)3 $(SELF2_OBJS)
	@echo ""
	@echo -e "${CYAN}--- Second-Generation Self-Host Summary ---${NC}"
	@if [ -f .success_list2 ]; then \
		echo -e "${GREEN}Successfully compiled with ./main2:${NC}"; \
		cat .success_list2 | sed 's/^/  - /'; \
	fi
	@if [ -f .failure_list2 ]; then \
		echo -e "\n${YELLOW}Failed with ./main2:${NC}"; \
		cat .failure_list2 | sed 's/^/  - /'; \
	fi
	@echo ""
	@echo -e "${GREEN}--- Second-generation self-host complete. Executable '$(TARGET)3' created. ---${NC}"

self2-test: self2
	@echo -e "\n${CYAN}--- Running tests after second-generation self-host... ---${NC}"
	./test.sh ./main3

self3: self2
	@echo -e "${CYAN}--- Starting third-generation self-host ---${NC}"
	rm -f .success_list3 .failure_list3
	mkdir -p $(SELF3_OUT)
	@echo -e "${CYAN}[1/4] Preprocessing source files...${NC}"
	for src in $(SRCS); do \
		$(CC) $(CPPFLAGS) $$src -o $(SELF3_OUT)/$$src; \
	done
	@echo -e "${CYAN}[2/4] Compiling with ./$(TARGET)3 (no fallback)...${NC}"
	for src in $(SRCS); do \
		s_file=$${src%.c}.s; \
		if ./$(TARGET)3 -i $(SELF3_OUT)/$$src -o $(SELF3_OUT)/$$s_file; then \
			echo -e "    [${GREEN}SUCCESS${NC}] Compiled '$${src}' with ./main3."; \
			echo "$$src" >> .success_list3; \
		else \
			echo -e "    [${YELLOW}FAIL${NC}] Failed on '$${src}' with ./main3."; \
			echo "$$src" >> .failure_list3; \
			exit 1; \
		fi; \
	done
	@echo -e "${CYAN}[3/4] Assembling generated sources...${NC}"
	for src in $(SRCS); do \
		s_file=$${src%.c}.s; \
		o_file=$${src%.c}.o; \
		$(CC) $(SELF_ASFLAGS) -c $(SELF3_OUT)/$$s_file -o $(SELF3_OUT)/$$o_file; \
	done
	@echo -e "${CYAN}[4/4] Linking to create $(TARGET)4...${NC}"
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(TARGET)4 $(SELF3_OBJS)
	@echo ""
	@echo -e "${CYAN}--- Third-Generation Self-Host Summary ---${NC}"
	@if [ -f .success_list3 ]; then \
		echo -e "${GREEN}Successfully compiled with ./main3:${NC}"; \
		cat .success_list3 | sed 's/^/  - /'; \
	fi
	@if [ -f .failure_list3 ]; then \
		echo -e "\n${YELLOW}Failed with ./main3:${NC}"; \
		cat .failure_list3 | sed 's/^/  - /'; \
	fi
	@echo ""
	@echo -e "${GREEN}--- Third-generation self-host complete. Executable '$(TARGET)4' created. ---${NC}"

self2-compare: self3
	@echo -e "\n${CYAN}--- Comparing self-hosted binaries... ---${NC}"
	@if cmp -s $(TARGET)3 $(TARGET)4; then \
		echo -e "${GREEN}Raw binaries match: $(TARGET)3 == $(TARGET)4${NC}"; \
	else \
		echo -e "${YELLOW}Raw binaries differ; checking stripped executable content...${NC}"; \
		cp $(TARGET)3 $(SELF3_OUT)/$(TARGET)3.compare; \
		cp $(TARGET)4 $(SELF3_OUT)/$(TARGET)4.compare; \
		strip --strip-debug --remove-section=.comment --remove-section=.note.gnu.build-id $(SELF3_OUT)/$(TARGET)3.compare $(SELF3_OUT)/$(TARGET)4.compare; \
		if cmp -s $(SELF3_OUT)/$(TARGET)3.compare $(SELF3_OUT)/$(TARGET)4.compare; then \
			echo -e "${GREEN}Stripped binaries match: $(TARGET)3 == $(TARGET)4${NC}"; \
		else \
			echo -e "${YELLOW}Self-hosted binaries differ: $(TARGET)3 != $(TARGET)4${NC}"; \
			exit 1; \
		fi; \
	fi

# Minimal self-host: only compile a safe subset (e.g., main.c) with ./main
self1-min: build
	@echo -e "${CYAN}--- Starting minimal self-host (subset: $(SAFE_SRCS)) ---${NC}"
	rm -f .success_list .failure_list
	mkdir -p $(SELF_OUT)
	@echo -e "${CYAN}[1/4] Preprocessing source files...${NC}"
	for src in $(SRCS); do \
		$(CC) $(CPPFLAGS) $$src -o $(SELF_OUT)/$$src; \
	done
	@echo -e "${CYAN}[2/4] Compiling subset with ./$(TARGET); others with gcc...${NC}"
	for src in $(SRCS); do \
		s_file=$${src%.c}.s; \
		if echo " $(SAFE_SRCS) " | grep -q " $$src "; then \
			if ./$(TARGET) -i $(SELF_OUT)/$$src -o $(SELF_OUT)/$$s_file; then \
				echo -e "    [${GREEN}SELF${NC}] Compiled '$${src}' with ./main."; \
				echo "$$src" >> .success_list; \
			else \
				echo -e "    [${YELLOW}FALLBACK${NC}] Failed on '$${src}'. Using gcc."; \
				echo "$$src" >> .failure_list; \
				$(CC) $(CFLAGS) -S -masm=intel -o $(SELF_OUT)/$$s_file $$src; \
			fi; \
		else \
			$(CC) $(CFLAGS) -S -masm=intel -o $(SELF_OUT)/$$s_file $$src; \
		fi; \
	done
	@echo -e "${CYAN}[3/4] Assembling generated sources...${NC}"
	for src in $(SRCS); do \
		s_file=$${src%.c}.s; \
		o_file=$${src%.c}.o; \
		$(CC) $(SELF_ASFLAGS) -c $(SELF_OUT)/$$s_file -o $(SELF_OUT)/$$o_file; \
	done
	@echo -e "${CYAN}[4/4] Linking to create $(TARGET)2...${NC}"
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(TARGET)2 $(SELF_OBJS)
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
	rm -f *.o $(TARGET) $(TARGET)2 $(TARGET)3 $(TARGET)4
	rm -f .success_list .failure_list .success_list2 .failure_list2 .success_list3 .failure_list3
	rm -rf out $(SELF_OUT) $(SELF2_OUT) $(SELF3_OUT)

.PHONY: all clean self1 self2 self3 test self1-test self1-smoke self2-test self2-compare build self1-min self1-min-test
