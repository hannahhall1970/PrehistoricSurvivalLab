#!/usr/bin/env python3
"""Validate an Unreal automation test report against tests declared in Source."""
from __future__ import annotations

import json
import re
import sys
from pathlib import Path

FAIL_PREFIX = "PSL_REPORT_FAIL:"
MACRO_RE = re.compile(
    r"\b(?:IMPLEMENT_SIMPLE_AUTOMATION_TEST|IMPLEMENT_COMPLEX_AUTOMATION_TEST|"
    r"IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST|BEGIN_DEFINE_SPEC|DEFINE_SPEC)\s*\("
)
PSL_LITERAL_RE = re.compile(r'"(PSL\.[^"]+)"')


def macro_arguments(text: str, start: int) -> list[str]:
    """Return comma-separated macro arguments, respecting nested calls/strings."""
    depth = 1
    argument: list[str] = []
    arguments: list[str] = []
    in_string = False
    escaped = False
    index = start
    while index < len(text):
        char = text[index]
        if in_string:
            argument.append(char)
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == '"':
                in_string = False
        elif char == '"':
            in_string = True
            argument.append(char)
        elif char == "(":
            depth += 1
            argument.append(char)
        elif char == ")":
            depth -= 1
            if depth == 0:
                arguments.append("".join(argument))
                return arguments
            argument.append(char)
        elif char == "," and depth == 1:
            arguments.append("".join(argument))
            argument = []
        else:
            argument.append(char)
        index += 1
    return []


def declared_tests(source_dir: str) -> list[str]:
    names: set[str] = set()
    root = Path(source_dir)
    if not root.is_dir():
        return []
    try:
        paths = sorted(
            path
            for path in root.rglob("*")
            if path.is_file() and path.suffix.lower() in {".cpp", ".h"}
        )
        for path in paths:
            try:
                text = path.read_text(encoding="utf-8")
            except (OSError, UnicodeError):
                continue
            for match in MACRO_RE.finditer(text):
                arguments = macro_arguments(text, match.end())
                if len(arguments) >= 2:
                    names.update(PSL_LITERAL_RE.findall(arguments[1]))
    except OSError:
        return []
    return sorted(names)


def counter(report: dict, key: str, errors: list[str]) -> int:
    value = report.get(key, 0)
    if isinstance(value, bool) or not isinstance(value, int):
        errors.append(f"header {key} is not an integer")
        return 0
    return value


def report_name(entry: object) -> str | None:
    if not isinstance(entry, dict):
        return None
    value = entry.get("fullTestPath")
    if not isinstance(value, str) or not value:
        value = entry.get("testDisplayName")
    return value if isinstance(value, str) and value else None


def is_psl_test(name: str | None) -> bool:
    return bool(name) and (name == "PSL" or name.startswith("PSL.") or ".PSL." in name)


def normalized(name: str) -> str:
    """Strip any UE prefix ("Project.Functional Tests.") so only the exact PSL.* path remains."""
    return name[name.index("PSL.") :] if "PSL." in name and not name.startswith("PSL.") else name


def summary(succeeded: object, warned: object, required: int, failed: object, not_run: object, report_path: str) -> None:
    print(
        f"PSL_REPORT: succeeded={succeeded} succeededWithWarnings={warned} required={required} "
        f"failed={failed} notRun={not_run} report={report_path}"
    )


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        print(f"{FAIL_PREFIX} usage: verify_test_report.py <index.json> <source_dir>")
        return 1

    report_path, source_dir = argv[1:]
    required = declared_tests(source_dir)
    errors: list[str] = []

    try:
        with open(report_path, encoding="utf-8-sig") as handle:
            report = json.load(handle)
    except (OSError, UnicodeError) as exc:
        summary(0, 0, len(required), 0, 0, report_path)
        print(f"{FAIL_PREFIX} unable to read report: {exc}")
        return 1
    except json.JSONDecodeError as exc:
        summary(0, 0, len(required), 0, 0, report_path)
        print(f"{FAIL_PREFIX} invalid JSON: {exc.msg}")
        return 1

    if not isinstance(report, dict):
        summary(0, 0, len(required), 0, 0, report_path)
        print(f"{FAIL_PREFIX} report root is not an object")
        return 1

    tests = report.get("tests")
    if not isinstance(tests, list):
        summary(report.get("succeeded", 0), report.get("succeededWithWarnings", 0), len(required), report.get("failed", 0), report.get("notRun", 0), report_path)
        print(f"{FAIL_PREFIX} report tests is not a list")
        return 1

    succeeded = counter(report, "succeeded", errors)
    succeeded_with_warnings = counter(report, "succeededWithWarnings", errors)
    failed = counter(report, "failed", errors)
    not_run = counter(report, "notRun", errors)
    psl_entries: list[tuple[str, object]] = []
    if not required:
        errors.append("no PSL tests declared in source")
    for entry in tests:
        name = report_name(entry)
        if is_psl_test(name):
            psl_entries.append((normalized(name), entry.get("state") if isinstance(entry, dict) else None))

    seen: dict[str, int] = {}
    for name, _ in psl_entries:
        seen[name] = seen.get(name, 0) + 1
    reported = set(seen)
    for name in required:
        if name not in reported:
            errors.append(f"required test missing from report: {name}")
    for name, count in seen.items():
        if count > 1:
            errors.append(f"duplicate report entries for PSL test: {name} x{count}")

    for name, state in psl_entries:
        if state != "Success":
            errors.append(f"PSL test failed: {name} state={state}")

    if failed != 0:
        errors.append(f"report header failed={failed}")
    if not_run != 0:
        errors.append(f"report header notRun={not_run}")
    if not tests:
        errors.append("report contains zero PSL tests")
    psl_success = sum(1 for _, state in psl_entries if state == "Success")
    if psl_success < len(required):
        errors.append("successful PSL test count is less than required test count")
    # UE header counters cover every test in the report, and a warned test keeps state "Success"
    # (AutomationControllerManager only splits succeeded/succeededWithWarnings in the header).
    all_success = sum(1 for entry in tests if isinstance(entry, dict) and entry.get("state") == "Success")
    if succeeded + succeeded_with_warnings != all_success:
        errors.append(
            "inconsistent success counters: "
            f"header succeeded+succeededWithWarnings={succeeded + succeeded_with_warnings} "
            f"entries with state Success={all_success}"
        )

    summary(succeeded, succeeded_with_warnings, len(required), failed, not_run, report_path)
    for error in errors:
        print(f"{FAIL_PREFIX} {error}")
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
