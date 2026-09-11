CC ?= clang
CFLAGS = -std=c99 -Wall -Wextra -O2 -D_POSIX_C_SOURCE=200809L
LDFLAGS = 

BUILD_DIR = build
SOURCE_DIR = Source
TEST_DIR = tests

SOURCES = $(filter-out $(SOURCE_DIR)/qrun_all.c,$(wildcard $(SOURCE_DIR)/*.c))
HEADERS = $(wildcard $(SOURCE_DIR)/*.h)
OBJECTS = $(SOURCES:$(SOURCE_DIR)/%.c=$(BUILD_DIR)/%.o)

TESTS = $(wildcard $(TEST_DIR)/*.c)
TEST_EXES = $(TESTS:$(TEST_DIR)/%.c=$(BUILD_DIR)/test_%)

TARGET = $(BUILD_DIR)/qrun

.PHONY: all clean test help

all: $(TARGET)

$(BUILD_DIR):
	@mkdir -p $@

$(BUILD_DIR)/%.o: $(SOURCE_DIR)/%.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJECTS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(OBJECTS) $(LDFLAGS) -o $@
	@echo "✓ Built: $@"

$(BUILD_DIR)/test_%: $(TEST_DIR)/test_%.c $(OBJECTS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(TEST_DIR)/test_$*.c $(filter-out $(BUILD_DIR)/qrun_main.o,$(OBJECTS)) -o $@

test: $(TEST_EXES)
	@echo "Running tests..."
	@for test in $(TEST_EXES); do \
		echo "  $$test"; \
		$$test || exit 1; \
	done
	@echo "✓ All tests passed"

clean:
	rm -rf $(BUILD_DIR)
	@echo "✓ Cleaned"

help:
	@echo "Q9-Run IR Interpreter"
	@echo "Targets:"
	@echo "  all    - Build qrun binary"
	@echo "  test   - Run unit tests"
	@echo "  clean  - Remove build output"
	@echo "  help   - Show this message"
