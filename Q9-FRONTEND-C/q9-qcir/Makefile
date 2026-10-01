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
	@$(TARGET) 'typedef int Count; int main(){ Count x=3; return x; }' > $(BUILD)/typedef.ir
	@grep -q '^FUNC main' $(BUILD)/typedef.ir
	@$(TARGET) 'typedef struct { int x; } Point; int main(){ Point p; p.x=1; return p.x; }' > $(BUILD)/typedef-struct.ir
	@grep -q '^FUNC main' $(BUILD)/typedef-struct.ir
	@$(TARGET) 'enum Color { RED, GREEN, BLUE }; int main(){ enum Color c; c=GREEN; return c; }' > $(BUILD)/enum.ir
	@grep -q '^PUSH 1$$' $(BUILD)/enum.ir
	@$(TARGET) 'int main(){ return sizeof("abc"); }' > $(BUILD)/sizeof-string.ir
	@grep -q '^PUSH 4$$' $(BUILD)/sizeof-string.ir
	@! grep -q '^GARRAY ' $(BUILD)/sizeof-string.ir
	@$(TARGET) 'int main(){ return sizeof("ab" "c"); }' > $(BUILD)/sizeof-concat.ir
	@grep -q '^PUSH 4$$' $(BUILD)/sizeof-concat.ir
	@$(TARGET) 'int main(){ return sizeof("a\n"); }' > $(BUILD)/sizeof-escape.ir
	@grep -q '^PUSH 3$$' $(BUILD)/sizeof-escape.ir
	@$(TARGET) 'modul interrupt int f(int n, ...){ return n; } int main(){ return f(1,2); }' > $(BUILD)/func-attrs.ir
	@grep -q '^FUNC f 1 0 3$$' $(BUILD)/func-attrs.ir
	@grep -q '^FUNC main 0 0 0$$' $(BUILD)/func-attrs.ir
	@if $(TARGET) 'int main(){ return (1; }' > $(BUILD)/invalid.ir 2> $(BUILD)/invalid.err; then exit 1; fi
	@grep -q '^FAIL$$' $(BUILD)/invalid.ir
	@echo 'qcir smoke test: OK'

clean:
	rm -rf $(BUILD)
