import { describe, expect, it } from "bun:test";
import { classifyLog } from "../src/classify.ts";

describe("classifyLog", () => {
	it("extracts UBT and clang compile diagnostics", () => {
		const result = classifyLog(
			"BUILD",
			1,
			"/path/PSLHUD.cpp(35,13): error: no matching member function for call to 'DrawText'\nPSLHUD.cpp:35:13: error: no matching member function for call to 'DrawText'",
		);
		expect(result.cls).toBe("COMPILE_ERROR");
		expect(result.excerpt).toContain("PSLHUD.cpp");
	});

	it("classifies linker failures", () => {
		expect(
			classifyLog(
				"BUILD",
				1,
				'Undefined symbols for architecture arm64\n  "_foo", referenced from:',
			).cls,
		).toBe("LINK_ERROR");
	});

	it("classifies automation failures and missing toolchains", () => {
		expect(
			classifyLog(
				"TEST",
				1,
				"LogAutomationController: Error: Test Completed. Result={Fail} Name={InitialValues} Path={PSL.Stats.InitialValues}",
			).cls,
		).toBe("TEST_FAILURE");
		expect(
			classifyLog("PREFLIGHT", 1, "Unreal Engine not found at: /missing").cls,
		).toBe("TOOLCHAIN_MISSING");
		expect(
			classifyLog(
				"PREFLIGHT",
				1,
				"xcode-select: error: tool 'xcodebuild' requires Xcode",
			).cls,
		).toBe("TOOLCHAIN_MISSING");
		// Exact messages from this repo's scripts while UE is absent; must park as BLOCKED, not TEST_FAILURE.
		for (const [gate, line] of [
			[
				"PREFLIGHT",
				"TOOLCHAIN_MISSING: Unreal Engine Build.sh missing or not executable: /UE/Build.sh",
			],
			["BUILD", "Unreal Build.sh not found or not executable: /UE/Build.sh"],
			[
				"TEST",
				"UnrealEditor-Cmd not found or not executable: /UE/UnrealEditor-Cmd",
			],
		] as const)
			expect(classifyLog(gate, 1, line).cls).toBe("TOOLCHAIN_MISSING");
	});

	it("limits fallback excerpts", () => {
		const result = classifyLog(
			"BUILD",
			1,
			Array.from({ length: 100 }, (_, index) => `line ${index}`).join("\n"),
		);
		expect(result.cls).toBe("UNKNOWN");
		expect(result.excerpt?.split("\n")).toHaveLength(40);
	});

	it("fails TEST on report verdict lines even when automation log is quiet", () => {
		const result = classifyLog(
			"TEST",
			1,
			"PSL_REPORT: succeeded=0 failed=0 notRun=0 total=0\nPSL_REPORT_FAIL: report contains zero PSL tests\n",
		);
		expect(result.cls).toBe("TEST_FAILURE");
		expect(result.summary).toContain("zero PSL tests");
	});

	it("classifies low disk as DISK_LOW, not a missing toolchain", () => {
		const result = classifyLog("PREFLIGHT", 1, "Disk available=1 KB\nDISK_LOW: 1 KB free, need 20 GB on /r\n");
		expect(result.cls).toBe("DISK_LOW");
	});

	it("classifies UE crash markers as ENGINE_CRASH", () => {
		for (const line of [
			"LogCore: Fatal error: [File:Foo.cpp] [Line: 1]",
			"Assertion failed: UploadDataProxy == nullptr [RendererPrivateUtils.h:317]",
			"Unhandled Exception: SIGSEGV: invalid attempt to read memory",
			"=== Critical error: ===",
			"Signal: SIGSEGV",
		])
			expect(classifyLog("TEST", 1, `starting\n${line}\n`).cls).toBe("ENGINE_CRASH");
	});

	it("does not let a crash marker mask a real test failure", () => {
		const log =
			"LogAutomationController: Error: Test Completed. Result={Fail} Name={X} Path={PSL.X}\nFatal error: shutdown\n";
		expect(classifyLog("TEST", 1, log).cls).toBe("TEST_FAILURE");
	});

	it("does not let toolchain noise mask a real test failure", () => {
		const log =
			"warning: No SDK found for plugin\nLogAutomationController: Error: Test Completed. Result={Fail} Name={X} Path={PSL.X}\n";
		expect(classifyLog("TEST", 1, log).cls).toBe("TEST_FAILURE");
	});
});
