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

$(BUILD)/native_platform_test: tests/native_platform_test.c src/platform_os9.c src/platform.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -Isrc -o $@ tests/native_platform_test.c

test: $(TARGET) $(BUILD)/native_platform_test
	sh tests/smoke.sh $(TARGET)
	$(BUILD)/native_platform_test
