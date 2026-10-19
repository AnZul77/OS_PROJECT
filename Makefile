CC ?= gcc
CFLAGS ?= -Wall -Wextra -O2 -Iinclude -pthread
LDFLAGS ?= -pthread

BUILD_DIR = build
BIN_DIR = bin

TARGET = $(BIN_DIR)/crash_archaeologist

SRCS = src/main.c \
       src/buffer/ring_buffer.c \
       src/collector/signal_handler.c \
       src/analyzer/correlator.c \
       src/reporter/report_generator.c

OBJS = $(SRCS:src/%.c=$(BUILD_DIR)/%.o)

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(OBJS) | $(BIN_DIR)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

$(BUILD_DIR)/%.o: src/%.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)

test:
	@echo "Run unit tests and workloads..."

HOOK_SRCS = src/collector/collector.c \
            src/collector/mem_hook.c \
            src/collector/thread_hook.c \
            src/collector/syscall_hook.c \
            src/collector/ipc_hook.c \
            src/buffer/ring_buffer.c

HOOK_TARGET = bin/libcrashhooks.so

.PHONY: hooks hook-test

$(HOOK_TARGET): $(HOOK_SRCS) include/collector.h include/event.h include/buffer.h | $(BIN_DIR)
	$(CC) $(CFLAGS) -fPIC -shared $(HOOK_SRCS) -o $@ -ldl -pthread

hooks: $(HOOK_TARGET)

hook-test: $(HOOK_TARGET)
	$(CC) $(CFLAGS) tests/workloads/hook_test.c -o bin/hook_test -pthread
	LD_PRELOAD="$(CURDIR)/$(HOOK_TARGET)" ./bin/hook_test
