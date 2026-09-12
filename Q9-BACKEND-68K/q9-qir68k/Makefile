CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra
BUILD ?= build

.PHONY: all clean
all: $(BUILD)/qir68k

$(BUILD)/qir68k: src/qcc_backend_c.cpp
	@mkdir -p $(BUILD)
	$(CC) -x c $(CFLAGS) -o $@ $<

clean:
	rm -rf $(BUILD)
