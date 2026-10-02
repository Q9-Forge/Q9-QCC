#!/bin/sh
# Copy a host tool to the macOS SDK command directory without clobbering it.
set -eu

if [ "$#" -ne 1 ]; then
	echo "usage: $0 <built-command>" >&2
	exit 2
fi

sdk_root=${Q9SDK:-"$HOME/Q9SDK"}
destination="$sdk_root/macOS/ARM64/CMDS/qcpp"

if [ -e "$destination" ]; then
	echo "qcpp: refusing to overwrite $destination" >&2
	exit 1
fi

mkdir -p "$sdk_root/macOS/ARM64/CMDS"
cp "$1" "$destination"
echo "qcpp copied to $destination"
