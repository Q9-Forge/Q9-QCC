CC ?= cc
CFLAGS ?= -std=c89 -O2 -Wall -Wextra -Werror
BUILD ?= build

.PHONY: all test clean
all: $(BUILD)/qost

$(BUILD)/qost: src/qost.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ $<

test: all
	@$(BUILD)/qost tests/constants.ir $(BUILD)/constants.opt.ir
	@diff -u tests/constants.expected.ir $(BUILD)/constants.opt.ir
	@$(BUILD)/qost tests/overflow.ir $(BUILD)/overflow.opt.ir
	@diff -u tests/overflow.ir $(BUILD)/overflow.opt.ir
	@$(BUILD)/qost tests/operators.ir $(BUILD)/operators.opt.ir
	@set -e; before=$$(python3 ../../tools/qccvm.py tests/constants.ir); \
	 after=$$(python3 ../../tools/qccvm.py $(BUILD)/constants.opt.ir); \
	 test "$$before" = "$$after"
	@set -e; before=$$(python3 ../../tools/qccvm.py tests/overflow.ir); \
	 after=$$(python3 ../../tools/qccvm.py $(BUILD)/overflow.opt.ir); \
	 test "$$before" = "$$after"
	@set -e; before=$$(python3 ../../tools/qccvm.py tests/operators.ir); \
	 after=$$(python3 ../../tools/qccvm.py $(BUILD)/operators.opt.ir); \
	 test "$$before" = "$$after"
	@echo 'qost tests: OK'

clean:
	rm -rf $(BUILD)
