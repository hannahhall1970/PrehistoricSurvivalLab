import { describe, expect, it } from "bun:test";
import { mkdirSync, mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join } from "node:path";
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

	it("accepts exactly the tests declared in the real source tree and rejects one missing", () => {
		const root = mkdtempSync(join(tmpdir(), "psl-report-real-"));
		try {
			const report = join(root, "index.json");
			const sourceDir = join(import.meta.dir, "../../Source");
			// Same discovery the validator uses, so the list tracks Source as tests are added.
			const discovered = Bun.spawnSync(
				[
					"python3",
					"-c",
					"import sys, json; sys.path.insert(0, sys.argv[1]); import verify_test_report as v; print(json.dumps(v.declared_tests(sys.argv[2])))",
					dirname(scriptPath),
					sourceDir,
				],
				{ stdout: "pipe", stderr: "pipe" },
			);
			const names: string[] = JSON.parse(
				new TextDecoder().decode(discovered.stdout),
			);
			expect(discovered.exitCode).toBe(0);
			expect(names).toContain("PSL.PIE.PlayerSpawn");
			expect(names).toContain("PSL.Survival.Stamina");
			const write = (list: string[]) =>
				writeReport(report, {
					succeeded: list.length,
					succeededWithWarnings: 0,
					failed: 0,
					notRun: 0,
					tests: list.map((name) => ({ fullTestPath: name, state: "Success" })),
				});
			write(names);
			expect(runValidator(report, sourceDir).exitCode).toBe(0);
			write(names.slice(1));
			const missing = runValidator(report, sourceDir);
			expect(missing.exitCode).toBe(1);
			expect(missing.output).toContain(names[0] as string);
		} finally {
			rmSync(root, { recursive: true, force: true });
		}
	});

	it("accepts warned tests in UE's real shape: entry state Success, header split", () => {
		// Same shape as .harness/evidence/pie-gui-20261112T144713Z/report/index.json (7 + 8 warned).
		withReport((report, sourceDir) => {
			writeReport(report, {
				succeeded: 1,
				succeededWithWarnings: 1,
				failed: 0,
				notRun: 0,
				tests: [
					{ fullTestPath: "PSL.Test.One", state: "Success", warnings: 0 },
					{ fullTestPath: "PSL.Test.Two", state: "Success", warnings: 1 },
				],
			});
			const result = runValidator(report, sourceDir);
			expect(result.output).toContain("succeededWithWarnings=1");
			expect(result.exitCode).toBe(0);
		});
	});

	it("counts non-PSL entries in the header the way UE does", () => {
		withReport((report, sourceDir) => {
			const base = passingReport();
			writeReport(report, {
				...base,
				succeeded: 3,
				tests: [...base.tests, { fullTestPath: "System.Other", state: "Success" }],
			});
			expect(runValidator(report, sourceDir).exitCode).toBe(0);
		});
	});

	it("fails duplicate report entries", () => {
		withReport((report, sourceDir) => {
			const base = passingReport();
			writeReport(report, {
				...base,
				succeeded: 3,
				tests: [...base.tests, { fullTestPath: "PSL.Test.One", state: "Success" }],
			});
			const result = runValidator(report, sourceDir);
			expect(result.exitCode).toBe(1);
			expect(result.output).toContain("duplicate report entries");
		});
	});

	it("does not let a suffix collision satisfy a required test", () => {
		withReport((report, sourceDir) => {
			writeReport(report, {
				succeeded: 2,
				succeededWithWarnings: 0,
				failed: 0,
				notRun: 0,
				tests: [
					{ fullTestPath: "PSL.Test.One", state: "Success" },
					{ fullTestPath: "PSL.X.PSL.Test.Two", state: "Success" },
				],
			});
			const result = runValidator(report, sourceDir);
			expect(result.exitCode).toBe(1);
			expect(result.output).toContain("required test missing from report: PSL.Test.Two");
		});
	});

	it("classifies a crash after a complete report as ENGINE_CRASH", () => {
		const log =
			"PSL_REPORT: succeeded=2 required=2 failed=0 notRun=0\nLogCore: Fatal error: Assertion failed: x\nPSL_REPORT_FAIL: report contains zero PSL tests";
		expect(classifyLog("TEST", 1, log).cls).toBe("ENGINE_CRASH");
	});
});
