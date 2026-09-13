CC      ?= cc
CFLAGS  ?= -std=c89 -O2 -Wall -Wextra
BUILD   ?= build
TARGET  := $(BUILD)/qcir
SOURCE  := data/qcc_p.c

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(SOURCE)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ $<

test: $(TARGET)
	@$(TARGET) 'int main(){ putint(42); }' > $(BUILD)/smoke.ir
	@test -s $(BUILD)/smoke.ir
	@grep -q '^FUNC main' $(BUILD)/smoke.ir
	@$(TARGET) 'int main(){ long x=42UL; return x; }' > $(BUILD)/integer-suffix.ir
	@grep -q '^PUSH 42$$' $(BUILD)/integer-suffix.ir
	@echo 'qcir smoke test: OK'

clean:
	rm -rf $(BUILD)
