#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
UE_ROOT="${UE_ROOT:-/Users/Shared/Epic Games/UE_5.8}"
PROJECT_FILE="$ROOT_DIR/PrehistoricSurvivalLab.uproject"
BUILD_SCRIPT="$UE_ROOT/Engine/Build/BatchFiles/Mac/Build.sh"

if [[ ! -d "$UE_ROOT" ]]; then
	echo "Unreal Engine not found at: $UE_ROOT" >&2
	exit 1
fi
if [[ ! -x "$BUILD_SCRIPT" ]]; then
	echo "Unreal Build.sh not found or not executable: $BUILD_SCRIPT" >&2
	exit 1
fi

"$BUILD_SCRIPT" PrehistoricSurvivalLabEditor Mac Development "-project=$PROJECT_FILE" -MaxParallelActions=4 -DisableAdaptiveUnity
