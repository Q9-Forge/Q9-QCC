CC      ?= cc
CFLAGS  ?= -std=c89 -O2 -Wall -Wextra
BUILD   ?= build
TARGET  := $(BUILD)/qcc

.PHONY: all clean test

all: $(TARGET) ../Q9-BACKEND-68K/q9-qir68k/build/qir68k ../Q9-BACKEND-68K/q9-qo68k/build/qo68k

../Q9-BACKEND-68K/q9-qir68k/build/qir68k:
	make -C ../Q9-BACKEND-68K/q9-qir68k

../Q9-BACKEND-68K/q9-qo68k/build/qo68k:
	make -C ../Q9-BACKEND-68K/q9-qo68k

$(TARGET): src/qcc.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ $<

test: $(TARGET)
	@$(TARGET) --version | grep -q '^qcc '
	@$(TARGET) --help | grep -q 'Usage:'
	@$(TARGET) --dry-run main.c | grep -q 'qcpp -> .i'
	@$(TARGET) --print-config | grep -q '^backend=qir68k'
	@$(TARGET) --emit-ir --tmpdir build/qcc-tmp tests/smoke.c >/dev/null
	@grep -q '^FUNC main' build/qcc-tmp/output.ir
	@$(TARGET) --emit-ir --tmpdir build/qcc-test -o build/qcc-test/result.ir tests/smoke.c >/dev/null
	@grep -q '^PUSH 42' build/qcc-test/result.ir
	@TMP=build/qcc-env $(TARGET) --emit-ir tests/smoke.c | grep -q 'build/qcc-env/qcc/output.ir'
	@$(TARGET) -E -o build/qcc-test/result.i tests/smoke.c >/dev/null
	@grep -q 'putint' build/qcc-test/result.i
	@$(TARGET) --dry-run --no-optimizer main.c | grep -q '3. qir68k'
	@$(TARGET) -c -o build/qcc-test/result.r tests/smoke.c >/dev/null
	@test -s build/qcc-test/result.r
	@$(TARGET) -o build/qcc-test/result.mod tests/smoke.c >/dev/null
	@test -s build/qcc-test/result.mod
	@echo 'qcc smoke test: OK'

clean:
	rm -rf $(BUILD)
