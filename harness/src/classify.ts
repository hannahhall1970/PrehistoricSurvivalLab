import type { Failure, FailureClass } from "./types.ts";

const excerpt = (lines: string[], limit = 60): string | undefined => {
	const kept = lines
		.map((line) => line.trimEnd())
		.filter(Boolean)
		.slice(0, limit);
	return kept.length ? kept.join("\n") : undefined;
};

const failure = (
	cls: FailureClass,
	summary: string,
	lines?: string[],
): Failure => ({
	cls,
	summary: summary.replace(/[\r\n]+/g, " ").trim(),
	...(lines?.length ? { excerpt: excerpt(lines) } : {}),
});

/** Classifies deterministic gate output; worker prose is intentionally not accepted here. */
export function classifyLog(
	gate: "PREFLIGHT" | "BUILD" | "TEST",
	exitCode: number | null,
	logText: string,
): Failure {
	const lines = logText.split(/\r?\n/);
	const nonEmpty = lines.filter((line) => line.trim());
	// Messages emitted by scripts/{preflight,build,run_tests}.sh and Xcode when the toolchain is absent.
	const toolchain = lines.filter((line) =>
		/TOOLCHAIN_MISSING|Unreal Engine not found|(Build\.sh|UnrealEditor-Cmd) not found|requires Xcode|xcode-select:\s*error|No SDK/i.test(
			line,
		),
	);
	if (toolchain.length)
		return failure(
			"TOOLCHAIN_MISSING",
			toolchain[0] ?? "Toolchain missing",
			toolchain,
		);

	const automation = lines.filter((line) =>
		/Test Completed\. Result=\{Fail\}|AutomationController:\s*Error:|LogAutomationController:\s*Error:/i.test(
			line,
		),
	);
	if (gate === "TEST" && automation.length) {
		return failure(
			"TEST_FAILURE",
			automation[0] ?? "Automation test failed",
			automation,
		);
	}

	const compile: string[] = [];
	const seen = new Set<string>();
	for (const line of lines) {
		if (!/\berror:\s*/i.test(line)) continue;
		if (
			/LogAutomationController:\s*Error:|Test Completed\. Result=\{Fail\}/i.test(
				line,
			)
		)
			continue;
		const match = line.match(
			/(?:^|\s)([^\s:()]+(?:\/[^\s:()]+)*)(?:\((\d+),(\d+)\)|:(\d+):(\d+)):\s*error:\s*(.+)$/i,
		);
		if (!match) continue;
		const key = line.trim();
		if (seen.has(key)) continue;
		seen.add(key);
		compile.push(line);
		if (compile.length === 60) break;
	}
	if (compile.length)
		return failure(
			"COMPILE_ERROR",
			compile[0] ?? "Compilation failed",
			compile,
		);

	const linker = lines.filter((line) =>
		/Undefined symbols for architecture\b|\bld:\s/i.test(line),
	);
	if (linker.length)
		return failure("LINK_ERROR", linker[0] ?? "Link failed", linker);

	if (gate === "TEST" && exitCode !== 0) {
		return failure(
			"TEST_FAILURE",
			nonEmpty.at(-1) ?? "Tests failed",
			nonEmpty.slice(-40),
		);
	}
	return failure(
		"UNKNOWN",
		nonEmpty.at(-1) ?? `${gate} failed`,
		nonEmpty.slice(-40),
	);
}
