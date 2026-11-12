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
});
