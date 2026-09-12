CC ?= cc
CFLAGS ?= -std=c89 -O2 -Wall -Wextra
BUILD ?= build

.PHONY: all clean
all: $(BUILD)/qo68k

$(BUILD)/qo68k: src/main.c src/qo68.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ src/main.c

clean:
	rm -rf $(BUILD)
