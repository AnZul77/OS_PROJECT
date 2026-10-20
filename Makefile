CC ?= gcc
CFLAGS ?= -Wall -Wextra -O2 -Iinclude -Isrc/analyzer -pthread
LDFLAGS ?= -pthread

BUILD_DIR = build
BIN_DIR = bin

TARGET = $(BIN_DIR)/crash_archaeologist

# Core flight recorder and analysis engine sources
CORE_SRCS = src/main.c \
            src/buffer/ring_buffer.c \
            src/collector/signal_handler.c \
            src/collector/state_collector.c \
            src/analyzer/correlator.c \
            src/analyzer/causal_graph.c \
            src/analyzer/root_cause.c \
            src/reporter/report_generator.c

CORE_OBJS = $(CORE_SRCS:src/%.c=$(BUILD_DIR)/%.o)

# Member 2 Hooks shared library
HOOK_SRCS = src/collector/collector.c \
            src/collector/mem_hook.c \
            src/collector/thread_hook.c \
            src/collector/syscall_hook.c \
            src/collector/ipc_hook.c \
            src/buffer/ring_buffer.c

HOOK_TARGET = $(BIN_DIR)/libcrashhooks.so

# Unit test binaries
TEST_RB = $(BIN_DIR)/test_ring_buffer
TEST_SIG = $(BIN_DIR)/test_signal_handler
TEST_ANALYZER_TARGET = $(BIN_DIR)/test_analyzer
TEST_GRAPH_TARGET = $(BIN_DIR)/test_causal_graph
BENCH_TARGET = $(BIN_DIR)/bench_analyzer

# Workloads
WORKLOAD_SRCS = tests/workloads/test_segfault.c \
                tests/workloads/test_uaf.c \
                tests/workloads/test_memleak.c \
                tests/workloads/test_race.c \
                tests/workloads/test_deadlock.c \
                tests/workloads/test_resource_exhaustion.c \
                tests/workloads/test_ipc_failure.c

WORKLOAD_BINS = $(patsubst tests/workloads/%.c,$(BIN_DIR)/%,$(WORKLOAD_SRCS))

.PHONY: all clean test test_dev1 test_analyzer test_causal_graph workloads bench_analyzer benchmark hooks hook-test

all: $(TARGET) workloads

$(TARGET): $(CORE_OBJS) | $(BIN_DIR)
	$(CC) $(CORE_OBJS) -o $@ $(LDFLAGS)

$(BUILD_DIR)/%.o: src/%.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

# --- Unit Tests ---
test: test_dev1 test_analyzer test_causal_graph

test_dev1: $(TEST_RB) $(TEST_SIG)
	@echo "=== Running Dev 1 Ring Buffer & Signal Unit Tests ==="
	./$(TEST_RB)
	./$(TEST_SIG)

$(TEST_RB): src/buffer/ring_buffer.c src/collector/state_collector.c tests/unit/test_ring_buffer.c | $(BIN_DIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(TEST_SIG): src/buffer/ring_buffer.c src/collector/state_collector.c src/collector/signal_handler.c tests/unit/test_signal_handler.c | $(BIN_DIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(TEST_ANALYZER_TARGET): tests/unit/test_analyzer.c \
                         tests/unit/mock_ring_buffer.c \
                         src/analyzer/correlator.c \
                         src/analyzer/causal_graph.c \
                         src/analyzer/root_cause.c \
                         src/reporter/report_generator.c | $(BIN_DIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

test_analyzer: $(TEST_ANALYZER_TARGET)
	@echo "=== Running Person 3 Causal Analyzer Unit Tests ==="
	./$(TEST_ANALYZER_TARGET)

$(TEST_GRAPH_TARGET): tests/unit/test_causal_graph.c \
                      tests/unit/mock_ring_buffer.c \
                      src/analyzer/correlator.c \
                      src/analyzer/causal_graph.c \
                      src/analyzer/root_cause.c \
                      src/reporter/report_generator.c | $(BIN_DIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

test_causal_graph: $(TEST_GRAPH_TARGET)
	@echo "=== Running Person 3 Causal Graph Unit Tests ==="
	./$(TEST_GRAPH_TARGET)

$(BENCH_TARGET): tests/unit/bench_analyzer.c \
                 tests/unit/mock_ring_buffer.c \
                 src/analyzer/correlator.c \
                 src/analyzer/causal_graph.c \
                 src/analyzer/root_cause.c \
                 src/reporter/report_generator.c | $(BIN_DIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

bench_analyzer: $(BENCH_TARGET)
	./$(BENCH_TARGET)

benchmark: bench_analyzer
	python scripts/benchmark_overhead.py

# --- Workloads ---
workloads: $(WORKLOAD_BINS)

$(BIN_DIR)/%: tests/workloads/%.c | $(BIN_DIR)
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

# --- Interception Hooks (Member 2) ---
$(HOOK_TARGET): $(HOOK_SRCS) include/collector.h include/event.h include/buffer.h | $(BIN_DIR)
	$(CC) $(CFLAGS) -fPIC -shared $(HOOK_SRCS) -o $@ -ldl -pthread

hooks: $(HOOK_TARGET)

hook-test: $(HOOK_TARGET)
	$(CC) $(CFLAGS) tests/workloads/hook_test.c -o $(BIN_DIR)/hook_test -pthread
	LD_PRELOAD="$(CURDIR)/$(HOOK_TARGET)" $(BIN_DIR)/hook_test

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)
