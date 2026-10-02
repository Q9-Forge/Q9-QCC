#!/bin/sh
set -eu
unset Q9SDK
program=$1
source_root=$(pwd)
test_root="${TMPDIR:-/tmp}/qmake-smoke-$$"
mkdir "$test_root"
trap 'rm -rf "$test_root"' EXIT HUP INT TERM
cp "$program" "$test_root/qmake"
cd "$test_root"
cp "$source_root/src/qmake.c" .
cp "$source_root/src/platform.h" .
./qmake -? > command-help.out
grep -q 'Usage:.*target' command-help.out
grep -q -- '-n --dry-run' command-help.out
./qmake --help > long-command-help.out
grep -q -- '-P NAME' long-command-help.out
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
./qmake all > build-status.out
test "$(wc -l < build-status.out | tr -d ' ')" = 1
grep -Eq 'result.txt \[global\].*[0-9]+\.[0-9][0-9] s ✓' build-status.out
test "$(cat result.txt)" = 'qmake test'
./qmake all > up-to-date-status.out
test "$(wc -l < up-to-date-status.out | tr -d ' ')" = 1
grep -q 'result.txt \[global\].*●' up-to-date-status.out
if grep -q ' s ' up-to-date-status.out; then
    echo 'up-to-date target unexpectedly has a build duration' >&2
    exit 1
fi
cat > alternate.mk <<'EOF'
alternate-target:
	echo alternate > alternate-output
EOF
./qmake -f alternate.mk alternate-target
test "$(cat alternate-output)" = alternate
rm alternate-output
./qmake --file alternate.mk alternate-target
test "$(cat alternate-output)" = alternate
rm alternate-output
if ./qmake -f > missing-file-option.out 2>&1; then
    echo 'missing -f argument was accepted' >&2
    exit 1
fi
grep -q 'requires a makefile path' missing-file-option.out
./qmake -v all > up-to-date.out
grep -q '●' up-to-date.out
./qmake -vv all > verbose.out
grep -q "checking prerequisite 'result.txt'" verbose.out
mkdir -p sdk/bin
cat > q9makefile <<'EOF'
OUTPUT = $(Q9SDK)/bin/output.txt
all: $(OUTPUT)
$(OUTPUT): input.txt
	cat input.txt > "$(OUTPUT)"
EOF
Q9SDK="$test_root/sdk" ./qmake all
test "$(cat sdk/bin/output.txt)" = 'qmake test'
Q9SDK="$test_root/sdk" ./qmake -v all > variable-target-up-to-date.out
grep -q '●' variable-target-up-to-date.out
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
[global]
SUBDIRS = child
help:
	echo parent-help-1 >> parent-help.log
	echo parent-help-2 >> parent-help.log
	echo parent-help-3 >> parent-help.log
	echo parent-help-4 >> parent-help.log
	echo parent-help-5 >> parent-help.log
	echo parent-help-6 >> parent-help.log
	echo parent-help-7 >> parent-help.log
	echo parent-help-8 >> parent-help.log
	echo parent-help-9 >> parent-help.log
	echo parent-help-10 >> parent-help.log
	echo parent-help-11 >> parent-help.log
	echo parent-help-12 >> parent-help.log
	echo parent-help-13 >> parent-help.log
	echo parent-help-14 >> parent-help.log
	echo parent-help-15 >> parent-help.log
	echo parent-help-16 >> parent-help.log
	echo parent-help-17 >> parent-help.log
	echo parent-help-18 >> parent-help.log
	echo parent-help-19 >> parent-help.log
	echo parent-help-20 >> parent-help.log
EOF
cat > child/q9makefile <<'EOF'
help:
	echo child-help >> child-help.log
EOF
./qmake > help-subdirs.out
test "$(wc -l < parent-help.log | tr -d ' ')" = 20
test ! -e child/child-help.log
subdirs=
i=1
while [ "$i" -le 24 ]; do
    child=$(printf 'many%02d' "$i")
    mkdir "$child"
    printf 'all:\n\techo child-build\n' > "$child/q9makefile"
    subdirs="$subdirs $child"
    i=$((i + 1))
done
printf 'SUBDIRS =%s\nall:\n' "$subdirs" > q9makefile
./qmake -n all > many-subdirs.out
test "$(grep -c '^echo child-build$' many-subdirs.out)" -eq 24
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
grep -q '●' marker-second.out
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
cp toolchains/test.conf toolchains/qmake.conf
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
sed '/^HOST_PLATFORM = /d' "$source_root/toolchains/qmake.conf" > toolchains/qmake.conf
cat > q9makefile <<'EOF'
[ACA]
[LGL]
[WCW]
[QQQ]
EOF
rm code.r
./qmake -n -P QQQ code.r > q9-profile.out
grep -q 'qcc .*--no-optimizer -c -o "code.r" "code.c"' q9-profile.out
./qmake -n -P ACA code.o > mac-profile.out
grep -q 'clang -std=c89 -c -o "code.o" "code.c"' mac-profile.out
./qmake -n -P LGL code.o > linux-profile.out
grep -q 'gcc -std=c89 -c -o "code.o" "code.c"' linux-profile.out
./qmake -n -P WCW code.o > windows-profile.out
grep -q 'clang -std=c89 -c -o "code.o" "code.c"' windows-profile.out
cat > q9makefile <<'EOF'
TOOLCHAIN_FILE = toolchains/test.conf
HOST_CC = sh fake-cc.sh
HOST_CFLAGS = --host-checked
TARGET_CFLAGS = --project-checked
EOF
./qmake code.r
test "$(cat code.r)" = 'C object'
./qmake -v code.r > implicit-current.out
grep -q 'is up to date' implicit-current.out
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
cat > toolchains/qmake.conf <<'EOF'
[global]
CONFIG_MARK = from-mac-profile
[mac-clang]
CONFIG_MARK = from-mac-profile
[q9-qcc-68k]
CONFIG_MARK = from-q9-profile
EOF
mkdir profile-dir
cat > profile-dir/qmake.conf <<'EOF'
[q9-qcc-68k]
CONFIG_MARK = from-config-dir
EOF
cat > sdk/Q9/68k/SYS/qmake.conf <<'EOF'
[global]
TARGET_CC = sh fake-cc.sh
TARGET_CPPFLAGS = -I$(DEFS)
TARGET_CFLAGS = --sdk-default
DEFS = /sdk/defs
[q9-qcc-68k]
CONFIG_MARK = from-sdk-profile
EOF
cat > q9makefile <<'EOF'
[global]
COMMON_MARK = common
[mac-clang]
CONFIG_MARK = from-mac-section
all:
	echo $(COMMON_MARK)-$(CONFIG_MARK) > mac.out
all_mac:
	echo mac-only > mac-only.out
[q9-qcc-68k]
TARGET_ARCH = 68k
all:
	echo $(COMMON_MARK)-$(CONFIG_MARK) > q9.out
all_q9:
	echo q9-only > q9-only.out
EOF
./qmake --list-configs > configs.out
grep -q '^mac-clang$' configs.out
grep -q '^q9-qcc-68k$' configs.out
if ./qmake -P unknown-config all > unknown-config.out 2>&1; then
    echo 'unknown configuration was not rejected' >&2
    exit 1
fi
grep -q 'unknown configuration' unknown-config.out
./qmake -n all > configs-dry-run.out
test ! -e mac.out
test ! -e q9.out
Q9SDK="$test_root/sdk" ./qmake all
test "$(cat mac.out)" = common-from-mac-section
test "$(cat q9.out)" = common-from-sdk-profile
./qmake all_mac
test -e mac-only.out
test ! -e q9-only.out
./qmake all_q9
test -e q9-only.out
Q9SDK="$test_root/sdk" ./qmake -P q9-qcc-68k all
test "$(cat q9.out)" = common-from-sdk-profile
./qmake -C profile-dir -P q9-qcc-68k all
test "$(cat q9.out)" = common-from-config-dir
cat > q9makefile <<'EOF'
[global]
help:
	echo help >> global-help.log
[mac-clang]
all:
	echo mac >> profile-builds.log
qcpp_mac:
	echo mac-only >> selected-profile.log
[mac-xcc-68k]
all:
	echo xcc >> profile-builds.log
qcpp_xcc:
	echo xcc-only >> selected-profile.log
EOF
rm -f help
./qmake
test "$(wc -l < global-help.log | tr -d ' ')" = 1
./qmake help
test "$(wc -l < global-help.log | tr -d ' ')" = 2
./qmake -n all > profile-all.out
test ! -e profile-builds.log
grep -q 'echo mac >> profile-builds.log' profile-all.out
grep -q 'echo xcc >> profile-builds.log' profile-all.out
./qmake all
test "$(wc -l < profile-builds.log | tr -d ' ')" = 2
./qmake qcpp_xcc
test "$(wc -l < selected-profile.log | tr -d ' ')" = 1
grep -q '^xcc-only$' selected-profile.log
cat > q9makefile <<'EOF'
[global]
TOOLCHAIN_FILE = toolchains/test.conf
[mac-clang]
all:
	echo mac >> profile-verbose.log
[mac-xcc-68k]
all:
	echo xcc >> profile-verbose.log
EOF
./qmake -v -n all > profile-verbose.out
test "$(grep -c "loading toolchain profile 'toolchains/test.conf'" profile-verbose.out)" = 1
cat > toolchains/host.conf <<'EOF'
[host-unavailable]
HOST_PLATFORM = nowhere
EOF
cat > q9makefile <<'EOF'
TOOLCHAIN_FILE = toolchains/host.conf
[host-unavailable]
all:
	echo should-not-run > host-skip.log
[host-neutral]
all:
	echo neutral >> host-skip.log
EOF
./qmake all > host-quiet.out
test ! -s host-quiet.out
test "$(cat host-skip.log)" = neutral
rm host-skip.log
./qmake -v all > host-verbose.out
grep -q "skipping configuration 'host-unavailable'" host-verbose.out
test "$(cat host-skip.log)" = neutral
if ./qmake -P host-unavailable all > host-explicit.out 2>&1; then
    echo 'explicit incompatible host configuration was accepted' >&2
    exit 1
fi
grep -q 'requires host' host-explicit.out
cat > q9makefile <<'EOF'
broken:
	echo intentional-build-diagnostic; false
EOF
if ./qmake broken > broken.out 2> broken.err; then
    echo 'failing recipe was accepted' >&2
    exit 1
fi
grep -q 'broken \[global\].*✗' broken.out
grep -q 'intentional-build-diagnostic' broken.err
grep -q 'recipe failed for .broken.' broken.err
cat > toolchains/helper.conf <<'EOF'
[mac-xqcc-68k]
EOF
cat > q9makefile <<'EOF'
TOOLCHAIN_FILE = toolchains/helper.conf
[mac-xqcc-68k]
all: $(Q9SDK)/macOS/ARM64/CMDS/helper
$(Q9SDK)/macOS/ARM64/CMDS/helper:
	mkdir -p "$(Q9SDK)/macOS/ARM64/CMDS" && echo helper > "$(Q9SDK)/macOS/ARM64/CMDS/helper"
EOF
Q9SDK="$test_root/sdk" ./qmake -P mac-xqcc-68k all > helper-first.out
grep -q 'helper (host) \[mac-xqcc-68k\].*✓' helper-first.out
Q9SDK="$test_root/sdk" ./qmake -P mac-xqcc-68k all > helper-current.out
test ! -s helper-current.out
i=1
: > q9makefile
while [ "$i" -le 48 ]; do
    printf 'rule%02d:\n\techo rule%02d\n' "$i" "$i" >> q9makefile
    i=$((i + 1))
done
./qmake -n rule48 > many-rules.out
grep -q '^echo rule48$' many-rules.out
echo 'qmake smoke test: OK'
