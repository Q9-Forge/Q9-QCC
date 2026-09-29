#!/bin/sh
set -eu
program=$1
source_root=$(pwd)
test_root="${TMPDIR:-/tmp}/qmake-smoke-$$"
mkdir "$test_root"
trap 'rm -rf "$test_root"' EXIT HUP INT TERM
cp "$program" "$test_root/qmake"
cd "$test_root"
cp "$source_root/src/qmake.c" .
cp "$source_root/src/platform.h" .
cat > q9makefile <<'EOF'
all: result.txt

result.txt: input.txt
	cat input.txt > result.txt
EOF
printf 'qmake test\n' > input.txt
./qmake '-DHOST_CC=cc' '-DHOST_CFLAGS=-std=c89 -pedantic -Wall -Wextra' -n qmake.o > native-dry-run.out
test ! -e qmake.o
grep -q 'cc -std=c89 -pedantic -Wall -Wextra -c' native-dry-run.out
./qmake '-DHOST_CC=cc' '-DHOST_CFLAGS=-std=c89 -pedantic -Wall -Wextra' qmake.o
test -s qmake.o
./qmake -n all > dry-run.out
test ! -e result.txt
grep -q 'cat input.txt > result.txt' dry-run.out
./qmake all
test "$(cat result.txt)" = 'qmake test'
./qmake -v all > up-to-date.out
grep -q 'up to date' up-to-date.out
./qmake -vv all > verbose.out
grep -q "checking prerequisite 'result.txt'" verbose.out
cat > q9makefile <<'EOF'
VALUE = echo from-file
result.txt:
	$(VALUE) > result.txt
EOF
rm result.txt
./qmake result.txt
test "$(cat result.txt)" = 'from-file'
rm result.txt
VALUE='echo from-environment' ./qmake result.txt
test "$(cat result.txt)" = 'from-environment'
rm result.txt
./qmake '-DVALUE=echo from-command-line' result.txt
test "$(cat result.txt)" = 'from-command-line'
cat > q9makefile <<'EOF'
VALUE = echo $(OTHER)
OTHER = recursive
result.txt:
	$(VALUE) > result.txt
EOF
rm result.txt
./qmake result.txt
test "$(cat result.txt)" = 'recursive'
cat > q9makefile <<'EOF'
cycle-a: cycle-b
cycle-b: cycle-a
EOF
if ./qmake cycle-a > cycle.out 2>&1; then
    echo 'cycle was not detected' >&2
    exit 1
fi
grep -q 'dependency cycle' cycle.out
mkdir child
cat > q9makefile <<'EOF'
SUBDIRS = child
all:
	test -f child/child-output && echo parent > parent-output
EOF
cat > child/q9makefile <<'EOF'
child-output:
	echo child > child-output
EOF
./qmake -n > subdir-dry-run.out
test ! -e child/child-output
test ! -e parent-output
grep -q 'echo child > child-output' subdir-dry-run.out
./qmake
test "$(cat child/child-output)" = child
test "$(cat parent-output)" = parent
cat > q9makefile <<'EOF'
help:
	echo help >> help.log
all:
	echo all >> all.log
EOF
./qmake
./qmake
test "$(wc -l < help.log | tr -d ' ')" = 2
./qmake all
test "$(wc -l < all.log | tr -d ' ')" = 1
touch help
./qmake
test "$(wc -l < help.log | tr -d ' ')" = 2
cat > q9makefile <<'EOF'
build: .built
	echo build-complete
.built: source.in
	echo compiling >> marker.log
	touch .built
EOF
printf 'input\n' > source.in
./qmake build > marker-first.out
./qmake -v build > marker-second.out
test "$(wc -l < marker.log | tr -d ' ')" = 1
grep -q 'up to date' marker-second.out
cat > q9makefile <<'EOF'
TOOLCHAIN_FILE = toolchains/test.conf
HOST_CC = sh fake-cc.sh
HOST_CFLAGS = --host-checked
TARGET_CFLAGS = --project-checked
EOF
mkdir toolchains
cat > toolchains/test.conf <<'EOF'
TARGET_CC = sh fake-cc.sh
TARGET_CPPFLAGS = -I$(DEFS)
TARGET_CFLAGS = --profile-checked
TARGET_AS = sh fake-as.sh
TARGET_ASFLAGS = --assembler-checked
TARGET_LD = fake-linker
TARGET_LDFLAGS = --linker-checked
DEFS = /shared/defs
LIBS = /shared/libs
STARTUP = cstart.r
TARGET_CPU = 68000
EOF
cat > fake-cc.sh <<'EOF'
#!/bin/sh
out=
while [ "$#" -gt 0 ]; do
    if [ "$1" = '-o' ]; then shift; out=$1; fi
    shift
done
test -n "$out"
printf 'C object\n' > "$out"
EOF
cat > fake-as.sh <<'EOF'
#!/bin/sh
for out do :; done
printf 'assembly object\n' > "$out"
EOF
printf 'int sample;\n' > code.c
mkdir -p sdk/Q9/68k/SYS home/Q9SDK/Q9/68k/SYS
cat > sdk/Q9/68k/SYS/qmake.conf <<'EOF'
TARGET_CC = sh fake-cc.sh
TARGET_CPPFLAGS = -I$(DEFS)
TARGET_CFLAGS = --sdk-default
DEFS = /sdk/defs
EOF
cat > home/Q9SDK/Q9/68k/SYS/qmake.conf <<'EOF'
TARGET_CC = sh fake-cc.sh
TARGET_CFLAGS = --home-fallback
EOF
cat > q9makefile <<'EOF'
TARGET_ARCH = 68k
EOF
Q9SDK="$test_root/sdk" ./qmake -n code.r > sdk-config.out
grep -q 'fake-cc.sh -I/sdk/defs --sdk-default -c' sdk-config.out
Q9SDK= HOME="$test_root/home" ./qmake -n code.r > home-config.out
grep -q 'fake-cc.sh .*--home-fallback -c' home-config.out
cat > q9makefile <<'EOF'
TOOLCHAIN_FILE = toolchains/test.conf
HOST_CC = sh fake-cc.sh
HOST_CFLAGS = --host-checked
TARGET_CFLAGS = --project-checked
EOF
./qmake -n code.r > implicit-c.out
test ! -e code.r
grep -q 'fake-cc.sh -I/shared/defs --project-checked -c' implicit-c.out
cat > q9makefile <<'EOF'
TOOLCHAIN_FILE = test.conf
HOST_CC = sh fake-cc.sh
HOST_CFLAGS = --host-checked
TARGET_CFLAGS = --project-checked
EOF
./qmake -C toolchains -n code.r > config-dir.out
grep -q 'fake-cc.sh -I/shared/defs --project-checked -c' config-dir.out
cat > q9makefile <<'EOF'
TOOLCHAIN_FILE = toolchains/test.conf
C_TO_R_COMMAND = $(TARGET_CC) $(TARGET_CFLAGS) -o "@TARGET@" "@SOURCE@"
ASM_TO_R_COMMAND = $(TARGET_AS) $(TARGET_ASFLAGS) "@SOURCE@" "@TARGET@"
EOF
./qmake -n code.r > template-c.out
grep -q 'fake-cc.sh .* -o .*code.r.*code.c' template-c.out
./qmake code.r
test "$(cat code.r)" = 'C object'
printf 'sample: rts\n' > asm.a
./qmake -n asm.r > template-asm.out
grep -q 'fake-as.sh --assembler-checked "asm.a" "asm.r"' template-asm.out
./qmake asm.r
test "$(cat asm.r)" = 'assembly object'
cat > q9makefile <<'EOF'
TOOLCHAIN_FILE = toolchains/test.conf
HOST_CC = sh fake-cc.sh
HOST_CFLAGS = --host-checked
TARGET_CFLAGS = --project-checked
EOF
./qmake code.r
test "$(cat code.r)" = 'C object'
./qmake -v code.r > implicit-current.out
grep -q 'up to date' implicit-current.out
./qmake -n code.o > implicit-host.out
test ! -e code.o
grep -q 'fake-cc.sh --host-checked -c' implicit-host.out
./qmake code.o
test "$(cat code.o)" = 'C object'
printf 'sample: rts\n' > asm.a
./qmake asm.r
test "$(cat asm.r)" = 'assembly object'
printf 'int plain;\n' > plain.c
cat > q9makefile <<'EOF'
TARGET_CFLAGS =
EOF
if ./qmake -DTARGET_CC= plain.r > no-target-compiler.out 2>&1; then
    echo 'missing target compiler was not diagnosed' >&2
    exit 1
fi
grep -q 'TARGET_CC is not configured' no-target-compiler.out
test ! -e plain.r
echo 'qmake smoke test: OK'
