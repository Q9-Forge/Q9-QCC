#!/bin/sh
# Install QCC's target headers/libraries and its Mac host profile into Q9SDK.
set -eu

repo=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
sdk=${Q9SDK:?set Q9SDK to the SDK root}
target="$sdk/Q9/68k"

mkdir -p "$sdk/macOS/ARM64/SYS" "$sdk/macOS/ARM64/CMDS" \
	"$sdk/macOS/x86_64/SYS" "$sdk/macOS/x86_64/CMDS" \
	"$sdk/Linux/SYS" "$sdk/Linux/CMDS" \
	"$sdk/Windows/SYS" "$sdk/Windows/CMDS" "$target/SYS" \
	"$target/DEFS" "$target/LIBS" "$target/CMDS" "$target/CMDS_CC" \
	"$target/CMDS_QCC" "$target/CMDS_XQCC" "$target/CMDS_XCC"
# qcc.conf/qmake.conf do not actually differ by host platform (same target
# backends, same section content either way) -- staged once per host
# directory anyway, because that is where qcc.c's load_config() and
# qmake's own host-profile search each look (macOS/<arch>/SYS, Linux/SYS,
# Windows/SYS; no architecture subfolder outside macOS, those CMDS trees
# are flat). Found missing for Linux only by actually running LQQ under
# WSL/Debian (2026-10-03) -- the macOS-only fix from the day before had
# no Linux/Windows counterpart yet.
for host_dir in macOS/ARM64 macOS/x86_64 Linux Windows; do
	cp "$repo/Q9-QCC/config/qcc-macos-sdk.conf" "$sdk/$host_dir/SYS/qcc.conf"
	cp "$repo/Q9-Make/toolchains/qmake.conf" "$sdk/$host_dir/SYS/qmake.conf"
done
cp "$repo/Q9-QCC/config/qcc-macos-sdk.conf" "$target/SYS/qcc.conf"
cp "$repo/Q9-FRONTEND-C/q9-qcpp/include/"*.h "$target/DEFS/"
# q9-qclib's own q9makefile builds per host-architecture under
# build/<config>/ (introduced when mac-clang-x86_64 was added); the flat
# build/q9_start.r / build/qclib.l are a stale pre-restructuring layout
# that stage_macos_sdk.sh kept copying from unnoticed. The 68k output is
# identical regardless of which host architecture built it, so any one
# config's build/ subdirectory is the correct, current source.
cp "$repo/Q9-BACKEND-68K/q9-qclib/build/mac-clang/q9_start.r" \
	"$repo/Q9-BACKEND-68K/q9-qclib/build/mac-clang/qclib.l" "$target/LIBS/"
echo "QCC SDK support files staged in $target"
