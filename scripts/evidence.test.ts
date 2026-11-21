import { afterEach, describe, expect, test } from "bun:test";
import { execFileSync } from "node:child_process";
import {
	cpSync,
	mkdirSync,
	mkdtempSync,
	readFileSync,
	rmSync,
	symlinkSync,
	utimesSync,
	writeFileSync,
} from "node:fs";
import { join } from "node:path";
import { tmpdir } from "node:os";

const repoScript = join(import.meta.dir, "evidence.ts");
const verifier = join(import.meta.dir, "verify_test_report.py");
const tempDirs: string[] = [];

function sh(cwd: string, command: string, args: string[] = []) {
	return execFileSync(command, args, {
		cwd,
		encoding: "utf8",
		stdio: ["ignore", "pipe", "pipe"],
	});
}
function cli(root: string, args: string[], env: Record<string, string> = {}) {
	const result = Bun.spawnSync([process.execPath, repoScript, ...args], {
		cwd: root,
		env: { ...process.env, ...env },
		stdout: "pipe",
		stderr: "pipe",
	});
	return {
		code: result.exitCode,
		stdout: result.stdout.toString(),
		stderr: result.stderr.toString(),
	};
}
function fixture() {
	const root = mkdtempSync(join(tmpdir(), "psl-evidence-"));
	tempDirs.push(root);
	mkdirSync(join(root, "Source"), { recursive: true });
	mkdirSync(join(root, "scripts"), { recursive: true });
	cpSync(verifier, join(root, "scripts/verify_test_report.py"));
	writeFileSync(
		join(root, "Source/Tests.cpp"),
		'#include "Tests.h"\nIMPLEMENT_SIMPLE_AUTOMATION_TEST(Foo, "PSL.Input.DefaultBindings", EAutomationTestFlags::EditorContext)\n',
	);
	sh(root, "git", ["init", "-q"]);
	sh(root, "git", ["config", "user.email", "test@example.invalid"]);
	sh(root, "git", ["config", "user.name", "Evidence Test"]);
	sh(root, "git", ["add", "."]);
	sh(root, "git", ["commit", "-qm", "fixture"]);
	return root;
}
function init(root: string, run = "run-1") {
	const result = cli(root, ["init", "--run", run, "--task", "T001"], {
		UE_ROOT: join(root, "UE"),
	});
	if (![0, 2].includes(result.code))
		throw new Error(`${result.code}: ${result.stderr}`);
	return join(root, ".harness/evidence", run);
}
function png(root: string) {
	const path = join(root, "shot.png");
	writeFileSync(
		path,
		Buffer.from(
			"iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+A8AAQUBAScY42YAAAAASUVORK5CYII=",
			"base64",
		),
	);
	return path;
}

afterEach(() => {
	for (const dir of tempDirs.splice(0))
		rmSync(dir, { recursive: true, force: true });
});

describe("evidence pipeline", () => {
	test("fingerprint changes when a Source file changes", () => {
		const root = fixture();
		const dir = init(root);
		const before = JSON.parse(
			readFileSync(join(dir, "manifest.json"), "utf8"),
		).sourceFingerprint;
		writeFileSync(join(root, "Source/Tests.cpp"), "changed\n");
		const verify = cli(root, ["verify", dir]);
		expect(verify.stdout).toContain('"status": "STALE"');
		expect(
			JSON.parse(readFileSync(join(dir, "verify.json"), "utf8")).fingerprint,
		).not.toBe(before);
	});

	test("verify marks stale evidence after a source edit", () => {
		const root = fixture();
		const dir = init(root);
		const shot = png(root);
		const capture = cli(root, [
			"add-screenshot",
			dir,
			shot,
			"--source",
			"fixture",
		]);
		if (capture.code !== 0)
			throw new Error(`${capture.code}: ${capture.stderr}`);
		writeFileSync(join(root, "Source/Tests.cpp"), "edited\n");
		const result = cli(root, ["verify", dir]);
		expect(result.code).toBe(1);
		expect(result.stdout).toContain('"status": "STALE"');
	});

	test("tampered artifact hash is detected", () => {
		const root = fixture();
		const dir = init(root);
		const shot = png(root);
		expect(
			cli(root, ["add-screenshot", dir, shot, "--source", "fixture"]).code,
		).toBe(0);
		const manifest = JSON.parse(
			readFileSync(join(dir, "manifest.json"), "utf8"),
		);
		writeFileSync(join(dir, manifest.artifacts[0].path), "tampered");
		const result = cli(root, ["verify", dir]);
		expect(result.code).toBe(1);
		expect(result.stdout).toContain('"status": "STALE"');
	});

	test("stale test report created before the gate is rejected", () => {
		const root = fixture();
		const dir = init(root);
		mkdirSync(join(root, "TestReports/old"), { recursive: true });
		writeFileSync(
			join(root, "TestReports/old/index.json"),
			JSON.stringify({
				tests: [],
				succeeded: 0,
				succeededWithWarnings: 0,
				failed: 0,
				notRun: 0,
			}),
		);
		const old = new Date(Date.now() - 10_000);
		// Gate discovery uses mtime, so make the fixture unambiguously stale.
		utimesSync(join(root, "TestReports/old/index.json"), old, old);
		const fake = join(root, "fake-gate.sh");
		writeFileSync(
			fake,
			'#!/bin/sh\nprintf \'{"gate":"TEST","outcome":"SUCCESS","exitCode":0,"durationMs":1,"command":["fake"],"logPath":"missing.log"}\n\'\n',
		);
		sh(root, "chmod", ["+x", fake]);
		const result = cli(root, ["gate", dir, "test"], {
			EVIDENCE_GATE_CMD: fake,
		});
		expect(result.code).toBe(1);
		expect(result.stdout).toContain("FAIL");
	});

	test("build lock contention is BLOCKED, not FAIL", () => {
		const root = fixture();
		const dir = init(root);
		const fake = join(root, "locked-gate.sh");
		// Mirrors harness cli: BUILD_LOCKED prints a FAILURE GateResult and exits 1.
		writeFileSync(
			fake,
			'#!/bin/sh\nprintf \'{"gate":"BUILD","outcome":"FAILURE","exitCode":null,"durationMs":0,"command":["bash"],"logPath":"missing.log","failure":{"cls":"BUILD_LOCKED","summary":"held"}}\\n\'\nexit 1\n',
		);
		sh(root, "chmod", ["+x", fake]);
		const result = cli(root, ["gate", dir, "build"], {
			EVIDENCE_GATE_CMD: fake,
		});
		expect(result.code).toBe(2);
		const manifest = JSON.parse(
			readFileSync(join(dir, "manifest.json"), "utf8"),
		);
		expect(manifest.gates.at(-1).status).toBe("BLOCKED");
	});

	test("exit 0 without a SUCCESS GateResult is FAIL, not PASS", () => {
		const root = fixture();
		const dir = init(root);
		const fake = join(root, "silent-gate.sh");
		writeFileSync(fake, "#!/bin/sh\necho built\nexit 0\n");
		sh(root, "chmod", ["+x", fake]);
		const result = cli(root, ["gate", dir, "build"], {
			EVIDENCE_GATE_CMD: fake,
		});
		expect(result.code).toBe(1);
		expect(result.stdout).toContain("without a SUCCESS GateResult");
	});

	// Fake test gate: writes the given reports and a contained log naming `named`.
	function reportGate(
		root: string,
		write: string[],
		named: string,
		report = '{"tests":[],"succeeded":0,"succeededWithWarnings":0,"failed":0,"notRun":0}',
	) {
		const fake = join(root, "report-gate.sh");
		const writes = write
			.map(
				(name) =>
					`mkdir -p TestReports/${name}\necho '${report}' > TestReports/${name}/index.json\n`,
			)
			.join("");
		writeFileSync(
			fake,
			`#!/bin/sh\n${writes}mkdir -p .harness/logs\necho "PSL_REPORT_PATH=$PWD/TestReports/${named}/index.json" > .harness/logs/test.log\nprintf '{"gate":"TEST","outcome":"SUCCESS","exitCode":0,"durationMs":1,"command":["fake"],"logPath":".harness/logs/test.log"}\\n'\n`,
		);
		sh(root, "chmod", ["+x", fake]);
		return fake;
	}

	test("the gate-named report is selected over an unrelated fresh one", () => {
		const root = fixture();
		const dir = init(root);
		const result = cli(root, ["gate", dir, "test"], {
			EVIDENCE_GATE_CMD: reportGate(root, ["mine", "other"], "mine"),
		});
		const record = JSON.parse(result.stdout);
		expect(record.reportPath).toMatch(/^reports\/test-/);
		expect(record.reportError).toBeUndefined();
		const archived = readFileSync(join(dir, record.reportPath), "utf8");
		expect(archived).toContain('"tests":[]');
	});

	test("real UE report shape: leaf testDisplayName, PSL path in fullTestPath", () => {
		const root = fixture();
		const dir = init(root);
		// Field layout copied from a real UE 5.8.4 TestReports/*/index.json.
		const report = JSON.stringify({
			succeeded: 1,
			succeededWithWarnings: 0,
			failed: 0,
			notRun: 0,
			tests: [
				{
					testDisplayName: "DefaultBindings",
					fullTestPath: "PSL.Input.DefaultBindings",
					state: "Success",
				},
			],
		});
		const result = cli(root, ["gate", dir, "test"], {
			EVIDENCE_GATE_CMD: reportGate(root, ["mine"], "mine", report),
		});
		expect(JSON.parse(result.stdout).tests).toEqual([
			{ name: "PSL.Input.DefaultBindings", state: "Success" },
		]);
	});

	test("an unrelated fresh report is refused when the named one is missing", () => {
		const root = fixture();
		const dir = init(root);
		const result = cli(root, ["gate", dir, "test"], {
			EVIDENCE_GATE_CMD: reportGate(root, ["other"], "mine"),
		});
		expect(result.code).toBe(1);
		expect(result.stdout).toContain("not created by this gate");
	});

	test("a screenshot cannot satisfy an automated criterion", () => {
		const root = fixture();
		const dir = init(root);
		expect(
			cli(root, [
				"add-screenshot",
				dir,
				png(root),
				"--source",
				"manual capture",
			]).code,
		).toBe(0);
		const result = cli(root, ["verify", dir]);
		expect(result.stdout).toContain('"id": "A3"');
		expect(result.stdout).toContain('"status": "BLOCKED"');
	});

	test("a symlinked screenshot source is refused", () => {
		const root = fixture();
		const dir = init(root);
		const link = join(root, "link.png");
		symlinkSync(png(root), link);
		const result = cli(root, [
			"add-screenshot",
			dir,
			link,
			"--source",
			"manual capture",
		]);
		expect(result.code).not.toBe(0);
		expect(result.stderr + result.stdout).toContain("not a regular file");
	});

	test("review with a mismatched fingerprint is rejected", () => {
		const root = fixture();
		const dir = init(root);
		const review = join(root, "review.json");
		writeFileSync(
			review,
			JSON.stringify({
				verdict: "approve",
				findings: [],
				reviewedFingerprint: "wrong",
			}),
		);
		const result = cli(root, ["add-review", dir, review]);
		expect(result.code).toBe(2);
		expect(result.stderr).toContain("fingerprint");
	});

	test("a gate log outside .harness/logs is not archived and fails the gate", () => {
		const root = fixture();
		const dir = init(root);
		const fake = join(root, "escape-gate.sh");
		writeFileSync(
			fake,
			'#!/bin/sh\nprintf \'{"gate":"BUILD","outcome":"SUCCESS","exitCode":0,"durationMs":1,"command":["x"],"logPath":"/etc/hosts"}\\n\'\n',
		);
		sh(root, "chmod", ["+x", fake]);
		const result = cli(root, ["gate", dir, "build"], {
			EVIDENCE_GATE_CMD: fake,
		});
		expect(result.code).toBe(1);
		expect(result.stdout).toContain("outside .harness/logs");
	});

	test("A7 MANUAL_PASS without the contract observations is refused", () => {
		const root = fixture();
		const dir = init(root);
		const result = cli(root, [
			"manual",
			dir,
			"A7",
			"MANUAL_PASS",
			"--by",
			"me",
			"--note",
			"looked fine",
		]);
		expect(result.code).not.toBe(0);
		expect(result.stderr).toContain("missing: wasd,mouse,sprint,release,bars");
	});

	test("a review finding without a disposition is refused", () => {
		const root = fixture();
		const dir = init(root);
		const review = join(root, "review.json");
		writeFileSync(
			review,
			JSON.stringify({
				verdict: "approve",
				findings: [{ id: "R1", evidence: "x" }],
				reviewedFingerprint: "any",
			}),
		);
		const result = cli(root, ["add-review", dir, review]);
		expect(result.code).not.toBe(0);
		expect(result.stderr).toContain("disposition");
	});
});
