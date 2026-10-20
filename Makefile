CC ?= gcc
CFLAGS ?= -Wall -Wextra -O2 -Iinclude -Isrc/analyzer -pthread
LDFLAGS ?= -pthread

BUILD_DIR = build
BIN_DIR = bin

TARGET = $(BIN_DIR)/crash_archaeologist

SRCS = src/main.c \
       src/buffer/ring_buffer.c \
       src/collector/signal_handler.c \
       src/analyzer/correlator.c \
       src/analyzer/causal_graph.c \
       src/analyzer/root_cause.c \
       src/reporter/report_generator.c

OBJS = $(SRCS:src/%.c=$(BUILD_DIR)/%.o)

TEST_ANALYZER_TARGET = $(BIN_DIR)/test_analyzer
TEST_ANALYZER_SRCS = tests/unit/test_analyzer.c \
                     tests/unit/mock_ring_buffer.c \
                     src/analyzer/correlator.c \
                     src/analyzer/causal_graph.c \
                     src/analyzer/root_cause.c \
                     src/reporter/report_generator.c

TEST_GRAPH_TARGET = $(BIN_DIR)/test_causal_graph
TEST_GRAPH_SRCS = tests/unit/test_causal_graph.c \
                  tests/unit/mock_ring_buffer.c \
                  src/analyzer/correlator.c \
                  src/analyzer/causal_graph.c \
                  src/analyzer/root_cause.c \
                  src/reporter/report_generator.c

WORKLOAD_SRCS = tests/workloads/test_segfault.c \
                tests/workloads/test_uaf.c \
                tests/workloads/test_memleak.c \
                tests/workloads/test_race.c \
                tests/workloads/test_deadlock.c \
                tests/workloads/test_resource_exhaustion.c \
                tests/workloads/test_ipc_failure.c

WORKLOAD_BINS = $(patsubst tests/workloads/%.c,$(BIN_DIR)/%,$(WORKLOAD_SRCS))

BENCH_TARGET = $(BIN_DIR)/bench_analyzer
BENCH_SRCS = tests/unit/bench_analyzer.c \
             tests/unit/mock_ring_buffer.c \
             src/analyzer/correlator.c \
             src/analyzer/causal_graph.c \
             src/analyzer/root_cause.c \
             src/reporter/report_generator.c

.PHONY: all clean test test_analyzer test_causal_graph workloads bench_analyzer benchmark

all: $(TARGET)

$(TARGET): $(OBJS) | $(BIN_DIR)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

$(BUILD_DIR)/%.o: src/%.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(TEST_ANALYZER_TARGET): $(TEST_ANALYZER_SRCS) | $(BIN_DIR)
	$(CC) $(CFLAGS) $(TEST_ANALYZER_SRCS) -o $@ $(LDFLAGS)

test_analyzer: $(TEST_ANALYZER_TARGET)
	@$(TEST_ANALYZER_TARGET)

$(TEST_GRAPH_TARGET): $(TEST_GRAPH_SRCS) | $(BIN_DIR)
	$(CC) $(CFLAGS) $(TEST_GRAPH_SRCS) -o $@ $(LDFLAGS)

test_causal_graph: $(TEST_GRAPH_TARGET)
	@$(TEST_GRAPH_TARGET)

$(BENCH_TARGET): $(BENCH_SRCS) | $(BIN_DIR)
	$(CC) $(CFLAGS) $(BENCH_SRCS) -o $@ $(LDFLAGS)

bench_analyzer: $(BENCH_TARGET)

benchmark: $(BENCH_TARGET)
	@$(BENCH_TARGET)

$(BIN_DIR)/test_%: tests/workloads/test_%.c | $(BIN_DIR)
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

workloads: $(WORKLOAD_BINS)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)

test: test_analyzer test_causal_graph
