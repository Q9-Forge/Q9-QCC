CC ?= cc
CFLAGS ?= -std=c89 -pedantic -O2 -Wall -Wextra
BUILD ?= build
PLATFORM ?= posix
ifeq ($(PLATFORM),windows)
PLATFORM_SOURCE = src/platform_windows.c
else ifeq ($(PLATFORM),posix)
PLATFORM_SOURCE = src/platform_posix.c
else
$(error unsupported PLATFORM '$(PLATFORM)'; choose posix or windows)
endif
SOURCES = src/qmake.c $(PLATFORM_SOURCE)
TARGET = $(BUILD)/qmake

.PHONY: all test
all: $(TARGET)

$(TARGET): $(SOURCES) src/platform.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -Isrc -o $@ $(SOURCES)

test: $(TARGET)
	sh tests/smoke.sh $(TARGET)
