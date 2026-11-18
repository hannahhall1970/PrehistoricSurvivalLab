import { describe, expect, it } from "bun:test";
import { mkdirSync, mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { classifyLog } from "../src/classify.ts";

const scriptPath = join(import.meta.dir, "../../scripts/verify_test_report.py");
const requiredNames = ["PSL.Test.One", "PSL.Test.Two"];

function makeSource(root: string, declarations = true): string {
	const sourceDir = join(root, "Source");
	const testDir = join(sourceDir, "X", "Private", "Tests");
	mkdirSync(testDir, { recursive: true });
	if (declarations) {
		writeFileSync(
			join(testDir, "T.cpp"),
			[
				'IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOne, "PSL.Test.One", EAutomationTestFlags::EditorContext)',
				'IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTwo, "PSL.Test.Two", EAutomationTestFlags::EditorContext)',
			].join("\n"),
		);
	}
	return sourceDir;
}

function runValidator(report: string, sourceDir: string) {
	const result = Bun.spawnSync(["python3", scriptPath, report, sourceDir], {
		stdout: "pipe",
		stderr: "pipe",
	});
	const output =
		new TextDecoder().decode(result.stdout) +
		new TextDecoder().decode(result.stderr);
	return { exitCode: result.exitCode, output };
}

function withReport(
	buildReport: (reportPath: string, sourceDir: string) => void,
	declarations = true,
) {
	const root = mkdtempSync(join(tmpdir(), "psl-report-"));
	try {
		const sourceDir = makeSource(root, declarations);
		const report = join(root, "index.json");
		buildReport(report, sourceDir);
	} finally {
		rmSync(root, { recursive: true, force: true });
	}
}

function writeReport(report: string, value: unknown) {
	writeFileSync(report, JSON.stringify(value));
}

function passingReport() {
	return {
		succeeded: 2,
		succeededWithWarnings: 0,
		failed: 0,
		notRun: 0,
		tests: requiredNames.map((name) => ({
			fullTestPath: `Project.Functional Tests.${name}`,
			state: "Success",
		})),
	};
}

describe("verify_test_report.py", () => {
	it("accepts complete reports with consistent counters", () => {
		withReport((report, sourceDir) => {
			writeReport(report, passingReport());
			const result = runValidator(report, sourceDir);
			expect(result.exitCode).toBe(0);
			expect(result.output).toContain("PSL_REPORT:");
		});
	});

	it("rejects an empty tests list", () => {
		withReport((report, sourceDir) => {
			writeReport(report, { succeeded: 0, failed: 0, notRun: 0, tests: [] });
			const result = runValidator(report, sourceDir);
			expect(result.exitCode).toBe(1);
			expect(result.output).toContain("PSL_REPORT_FAIL");
		});
	});

	it("reports a missing required test by name", () => {
		withReport((report, sourceDir) => {
			writeReport(report, {
				succeeded: 1,
				failed: 0,
				notRun: 0,
				tests: [{ fullTestPath: "PSL.Test.One", state: "Success" }],
			});
			const result = runValidator(report, sourceDir);
			expect(result.exitCode).toBe(1);
			expect(result.output).toContain("PSL.Test.Two");
			expect(classifyLog("TEST", 1, result.output).cls).toBe("TEST_FAILURE");
		});
	});

	it("rejects a failed PSL test state", () => {
		withReport((report, sourceDir) => {
			writeReport(report, {
				succeeded: 1,
				failed: 1,
				notRun: 0,
				tests: [
					{ fullTestPath: "PSL.Test.One", state: "Success" },
					{ fullTestPath: "PSL.Test.Two", state: "Fail" },
				],
			});
			const result = runValidator(report, sourceDir);
			expect(result.exitCode).toBe(1);
			expect(result.output).toContain("state=Fail");
		});
	});

	it("rejects a non-zero notRun header", () => {
		withReport((report, sourceDir) => {
			writeReport(report, { ...passingReport(), notRun: 1 });
			const result = runValidator(report, sourceDir);
			expect(result.exitCode).toBe(1);
			expect(result.output).toContain("notRun");
		});
	});

	it("rejects inconsistent succeeded counters", () => {
		withReport((report, sourceDir) => {
			writeReport(report, { ...passingReport(), succeeded: 5 });
			const result = runValidator(report, sourceDir);
			expect(result.exitCode).toBe(1);
			expect(result.output).toContain("inconsistent");
		});
	});

	it("rejects malformed JSON without a Python traceback", () => {
		withReport((report, sourceDir) => {
			writeFileSync(report, "{not json");
			const result = runValidator(report, sourceDir);
			expect(result.exitCode).toBe(1);
			expect(result.output).toContain("PSL_REPORT_FAIL");
			expect(result.output).not.toContain("Traceback");
		});
	});

	it("rejects a source tree with no PSL declarations", () => {
		withReport((report, sourceDir) => {
			writeReport(report, { succeeded: 0, failed: 0, notRun: 0, tests: [] });
			const result = runValidator(report, sourceDir);
			expect(result.exitCode).toBe(1);
			expect(result.output).toContain("no PSL tests declared in source");
		}, false);
	});

	it("matches the fifteen current tests in the real source tree", () => {
		const root = mkdtempSync(join(tmpdir(), "psl-report-real-"));
		try {
			const report = join(root, "index.json");
			const sourceDir = join(import.meta.dir, "../../Source");
			const names = [
				"PSL.Stats.InitialValues",
				"PSL.Survival.Stamina",
				"PSL.Survival.ZeroStaminaRequiresRelease",
				"PSL.Stats.DamageAndDeath",
				"PSL.Hud.BarFraction",
				"PSL.Input.DefaultBindings",
				"PSL.Character.SprintSpeed",
				"PSL.PIE.PlayerSpawn",
				"PSL.PIE.MoveInput",
				"PSL.PIE.LookInput",
				"PSL.PIE.JumpInput",
				"PSL.PIE.SprintInput",
				"PSL.PIE.StaminaExhaustion",
				"PSL.PIE.SprintRelease",
				"PSL.PIE.HUD",
			];
			writeReport(report, {
				succeeded: names.length,
				succeededWithWarnings: 0,
				failed: 0,
				notRun: 0,
				tests: names.map((name) => ({ fullTestPath: name, state: "Success" })),
			});
			const result = runValidator(report, sourceDir);
			expect(result.exitCode).toBe(0);
		} finally {
			rmSync(root, { recursive: true, force: true });
		}
	});
});
