CC ?= cc
CFLAGS ?= -std=c99 -O2 -Wall -Wextra
BUILD ?= build

.PHONY: all clean test
all: $(BUILD)/qrx86

$(BUILD)/qrx86: src/qrx86.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ $<

test: $(BUILD)/qrx86
	$(BUILD)/qrx86 tests/tool_backend_smoke.sx86 $(BUILD)/tool_backend_smoke.r
	test -s $(BUILD)/tool_backend_smoke.r

clean:
	rm -rf $(BUILD)
