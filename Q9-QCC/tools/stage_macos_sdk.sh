#!/bin/sh
# Install QCC's target headers/libraries and its Mac host profile into Q9SDK.
set -eu

repo=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
sdk=${Q9SDK:?set Q9SDK to the SDK root}
target="$sdk/Q9/68k"

mkdir -p "$sdk/Mac/SYS" "$target/SYS" "$target/DEFS" "$target/LIBS" \
	"$target/CMDS" "$target/CMDS_QCC" "$target/CMDS_XQCC" "$target/CMDS_XCC"
cp "$repo/Q9-QCC/config/qcc-macos-sdk.conf" "$sdk/Mac/SYS/qcc.conf"
cp "$repo/Q9-QCC/config/qcc-macos-sdk.conf" "$target/SYS/qcc.conf"
cp "$repo/Q9-FRONTEND-C/q9-qcpp/include/"*.h "$target/DEFS/"
cp "$repo/Q9-BACKEND-68K/q9-qclib/build/q9_start.r" \
	"$repo/Q9-BACKEND-68K/q9-qclib/build/qclib.l" "$target/LIBS/"
echo "QCC SDK support files staged in $target"
