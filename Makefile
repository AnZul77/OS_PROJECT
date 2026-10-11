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
