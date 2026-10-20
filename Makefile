CC ?= gcc
CFLAGS ?= -Wall -Wextra -O2 -Iinclude -pthread
LDFLAGS ?= -pthread

BUILD_DIR = build
BIN_DIR = bin

TARGET = $(BIN_DIR)/crash_archaeologist
TEST_RB = $(BIN_DIR)/test_ring_buffer
TEST_SIG = $(BIN_DIR)/test_signal_handler

DEV1_SRCS = src/main.c \
            src/buffer/ring_buffer.c \
            src/collector/signal_handler.c \
            src/collector/state_collector.c

DEV1_OBJS = $(DEV1_SRCS:src/%.c=$(BUILD_DIR)/%.o)

.PHONY: all clean test dev1

all: dev1

dev1: $(TARGET)

$(TARGET): $(DEV1_OBJS) | $(BIN_DIR)
	$(CC) $(DEV1_OBJS) -o $@ $(LDFLAGS)

$(BUILD_DIR)/%.o: src/%.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

test: $(TEST_RB) $(TEST_SIG)
	@echo "Running Dev 1 Unit Tests..."
	./$(TEST_RB)
	./$(TEST_SIG)

$(TEST_RB): src/buffer/ring_buffer.c src/collector/state_collector.c tests/unit/test_ring_buffer.c | $(BIN_DIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(TEST_SIG): src/buffer/ring_buffer.c src/collector/state_collector.c src/collector/signal_handler.c tests/unit/test_signal_handler.c | $(BIN_DIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)
