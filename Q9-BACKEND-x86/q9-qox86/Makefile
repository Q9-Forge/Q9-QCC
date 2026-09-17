CC      ?= cc
CFLAGS  ?= -std=c11 -O2 -Wall -Wextra
BUILD   ?= build
TARGET  := $(BUILD)/qox86
SOURCE  := src/qox86.c

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SOURCE)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -rf $(BUILD)
