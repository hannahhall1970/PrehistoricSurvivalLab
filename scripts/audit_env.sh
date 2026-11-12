#!/usr/bin/env bash
set -euo pipefail

UE_ROOT="${UE_ROOT:-/Users/Shared/Epic Games/UE_5.8}"

echo "== macOS =="
sw_vers || true

echo "== Xcode select =="
xcode-select -p 2>&1 || true
echo "== xcodebuild =="
xcodebuild -version 2>&1 || true
echo "== git-lfs =="
git lfs version 2>&1 || true
echo "== disk =="
df -h .
echo "== memory =="
sysctl -n hw.memsize 2>&1 || true
echo "== Unreal Engine =="
if [[ -d "$UE_ROOT" ]]; then
	echo "UE_ROOT=$UE_ROOT (present)"
else
	echo "UE_ROOT=$UE_ROOT (missing)"
fi
