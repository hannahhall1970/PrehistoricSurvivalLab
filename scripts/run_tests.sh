#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
UE_ROOT="${UE_ROOT:-/Users/Shared/Epic Games/UE_5.8}"
PROJECT_FILE="$ROOT_DIR/PrehistoricSurvivalLab.uproject"
EDITOR_CMD="$UE_ROOT/Engine/Binaries/Mac/UnrealEditor-Cmd"
REPORT_DIR="$ROOT_DIR/TestReports/$(date +%Y%m%d-%H%M%S)"

if [[ ! -d "$UE_ROOT" ]]; then
	echo "Unreal Engine not found at: $UE_ROOT" >&2
	exit 1
fi
if [[ ! -x "$EDITOR_CMD" ]]; then
	echo "UnrealEditor-Cmd not found or not executable: $EDITOR_CMD" >&2
	exit 1
fi

mkdir -p "$REPORT_DIR"
"$EDITOR_CMD" "$PROJECT_FILE" -ExecCmds="Automation RunTests PSL.;Quit" -unattended -nullrhi -nosplash "-ReportExportPath=$REPORT_DIR"
