#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
UE_ROOT="${UE_ROOT:-/Users/Shared/Epic Games/UE_5.8}"
BUILD_SCRIPT="$UE_ROOT/Engine/Build/BatchFiles/Mac/Build.sh"
failures=()

printf '== PrehistoricSurvivalLab preflight ==\n'
printf 'ROOT_DIR=%s\n' "$ROOT_DIR"
printf 'UE_ROOT=%s\n' "$UE_ROOT"

if [[ -x "$BUILD_SCRIPT" ]]; then
	printf 'UE Build.sh=%s (present)\n' "$BUILD_SCRIPT"
else
	printf 'UE Build.sh=%s (missing)\n' "$BUILD_SCRIPT"
	failures+=("Unreal Engine Build.sh missing or not executable: $BUILD_SCRIPT")
fi

if XCODE_OUTPUT="$(xcodebuild -version 2>&1)"; then
	printf '%s\n' "$XCODE_OUTPUT"
	XCODE_VERSION="$(printf '%s\n' "$XCODE_OUTPUT" | awk '/^Xcode / { print $2; exit }')"
	if [[ "$XCODE_VERSION" =~ ^([0-9]+)\.([0-9]+)(\.[0-9]+)?$ ]]; then
		XCODE_MAJOR="${BASH_REMATCH[1]}"
		XCODE_MINOR="${BASH_REMATCH[2]}"
		if ((XCODE_MAJOR > 26 || (XCODE_MAJOR == 26 && XCODE_MINOR >= 4))); then
			failures+=("Xcode $XCODE_VERSION is unsupported (26.4+)")
		elif [[ ! "$XCODE_VERSION" =~ ^26\.1\.[0-9]+$ ]]; then
			printf 'WARNING: expected Xcode 26.1.x, found %s\n' "$XCODE_VERSION"
		fi
	else
		failures+=("Unable to parse xcodebuild version: $XCODE_VERSION")
	fi
else
	printf '%s\n' "$XCODE_OUTPUT"
	failures+=("xcodebuild is unavailable")
fi

DISK_AVAILABLE_KB="$(df -Pk "$ROOT_DIR" | awk 'NR == 2 { print $4 }')"
printf 'Disk available=%s KB\n' "${DISK_AVAILABLE_KB:-unknown}"
if [[ ! "$DISK_AVAILABLE_KB" =~ ^[0-9]+$ ]] || ((DISK_AVAILABLE_KB < 20 * 1024 * 1024)); then
	failures+=("less than 20 GB free disk space")
fi

if ((${#failures[@]} > 0)); then
	for failure in "${failures[@]}"; do
		printf 'TOOLCHAIN_MISSING: %s\n' "$failure" >&2
	done
	exit 1
fi

printf 'Preflight OK\n'
