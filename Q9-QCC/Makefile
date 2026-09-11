CC      ?= cc
CFLAGS  ?= -std=c89 -O2 -Wall -Wextra
BUILD   ?= build
TARGET  := $(BUILD)/qcc

.PHONY: all clean test

all: $(TARGET)

$(TARGET): src/qcc.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ $<

test: $(TARGET)
	@$(TARGET) --version | grep -q '^qcc '
	@$(TARGET) --help | grep -q 'Usage:'
	@$(TARGET) --dry-run main.c | grep -q 'qcpp -> .i'
	@$(TARGET) --print-config | grep -q '^backend=qir68k'
	@$(TARGET) --emit-ir tests/smoke.c >/dev/null
	@grep -q '^FUNC main' build/qcc-tmp/output.ir
	@echo 'qcc smoke test: OK'

clean:
	rm -rf $(BUILD)
