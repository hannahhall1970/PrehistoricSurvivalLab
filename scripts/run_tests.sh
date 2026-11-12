#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
UE_ROOT="${UE_ROOT:-/Users/Shared/Epic Games/UE_5.8}"
PROJECT_FILE="$ROOT_DIR/PrehistoricSurvivalLab.uproject"
EDITOR_CMD="$UE_ROOT/Engine/Binaries/Mac/UnrealEditor-Cmd"

if [[ ! -d "$UE_ROOT" ]]; then
	echo "Unreal Engine not found at: $UE_ROOT" >&2
	exit 1
fi
if [[ ! -x "$EDITOR_CMD" ]]; then
	echo "UnrealEditor-Cmd not found or not executable: $EDITOR_CMD" >&2
	exit 1
fi

mkdir -p "$ROOT_DIR/TestReports"
REPORT_DIR="$(mktemp -d "$ROOT_DIR/TestReports/$(date +%Y%m%d-%H%M%S).XXXXXX")"
if [[ -e "$REPORT_DIR/index.json" ]]; then
	echo "PSL_REPORT_FAIL: stale report present" >&2
	exit 1
fi
set +e
"$EDITOR_CMD" "$PROJECT_FILE" -ExecCmds="Automation RunTests PSL.;Quit" -unattended -nullrhi -nosplash "-ReportExportPath=$REPORT_DIR"
EDITOR_EXIT=$?
set -e

# The Editor can exit 0 with zero or failed tests; the exported report is the verdict.
REPORT="$REPORT_DIR/index.json"
if [[ ! -f "$REPORT" ]]; then
	echo "PSL_REPORT_FAIL: no automation report at $REPORT (editor exit $EDITOR_EXIT)" >&2
	exit 1
fi
REPORT_EXIT=0
/usr/bin/env python3 "$ROOT_DIR/scripts/verify_test_report.py" "$REPORT" "$ROOT_DIR/Source" || REPORT_EXIT=$?
if [[ $EDITOR_EXIT -ne 0 ]]; then
	exit "$EDITOR_EXIT"
fi
exit "$REPORT_EXIT"
