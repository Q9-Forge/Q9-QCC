CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra
BUILD ?= build

.PHONY: all clean
all: $(BUILD)/qirx86

$(BUILD)/qirx86: src/qcc_backend_c.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -rf $(BUILD)
