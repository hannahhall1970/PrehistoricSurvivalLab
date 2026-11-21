import { createHash } from "node:crypto";
import { spawnSync } from "node:child_process";
import {
	copyFileSync,
	existsSync,
	lstatSync,
	mkdirSync,
	readdirSync,
	readFileSync,
	realpathSync,
	statSync,
	writeFileSync,
} from "node:fs";
import { basename, join, resolve } from "node:path";

export type EvidenceStatus =
	| "AUTOMATED_PASS"
	| "VISUAL_VERIFIED"
	| "MANUAL_PENDING"
	| "MANUAL_PASS"
	| "FAIL"
	| "BLOCKED"
	| "STALE";

type Artifact = { path: string; sha256: string; bytes: number; kind: string };
type CommandRecord = {
	argv: string[];
	exitCode: number | null;
	startedAt: string;
	endedAt: string;
};
type Manifest = {
	runId: string;
	taskId: string;
	createdAt: string;
	gitHead: string;
	gitStatus: string;
	sourceFingerprint: string;
	artifacts: Artifact[];
	commands: CommandRecord[];
	status: EvidenceStatus;
	blockers: string[];
	gates: Record<string, unknown>[];
	screenshots: Record<string, unknown>[];
	reviews: Record<string, unknown>[];
	manual: Record<string, unknown>[];
};

type Fingerprint = {
	fingerprint: string;
	files: { path: string; sha256: string }[];
};
type RunResult = {
	code: number | null;
	stdout: string;
	stderr: string;
	error?: string;
};

function now(): string {
	return new Date().toISOString();
}
function json(value: unknown): string {
	return JSON.stringify(value, null, 2) + "\n";
}
function run(argv: string[], cwd: string): RunResult {
	try {
		const result = spawnSync(argv[0], argv.slice(1), {
			cwd,
			encoding: "utf8",
			maxBuffer: 32 * 1024 * 1024,
		});
		return {
			code: result.status,
			stdout: result.stdout ?? "",
			stderr: result.stderr ?? "",
			error: result.error?.message,
		};
	} catch (error) {
		return {
			code: null,
			stdout: "",
			stderr: "",
			error: error instanceof Error ? error.message : String(error),
		};
	}
}
function command(
	argv: string[],
	cwd: string,
): RunResult & { startedAt: string; endedAt: string } {
	const startedAt = now();
	const result = run(argv, cwd);
	return { ...result, startedAt, endedAt: now() };
}
function git(root: string, args: string[]): string {
	const result = run(["git", ...args], root);
	if (result.code !== 0)
		throw new Error(
			result.error || result.stderr.trim() || `git ${args.join(" ")} failed`,
		);
	return result.stdout.trim();
}
function repoRoot(): string {
	return git(process.cwd(), ["rev-parse", "--show-toplevel"]);
}
function sha256(data: Buffer | string): string {
	return createHash("sha256").update(data).digest("hex");
}
function filesForFingerprint(root: string): string[] {
	const result = run(
		["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard"],
		root,
	);
	if (result.code !== 0)
		throw new Error(
			result.error || result.stderr.trim() || "git ls-files failed",
		);
	return result.stdout
		.split("\0")
		.filter(Boolean)
		.filter(
			(path) =>
				path.startsWith("Source/") ||
				path.startsWith("Config/") ||
				path.startsWith("Content/") ||
				path.startsWith("harness/src/") ||
				path.startsWith("Plugins/") ||
				path.startsWith("scripts/") ||
				path.endsWith(".uproject") ||
				path === "harness/harness.config.json" ||
				path === ".gitattributes" ||
				// Review inputs: a review of an older skill/config/contract must go stale.
				path.startsWith(".omp/") ||
				path.startsWith("docs/tasks/") ||
				path === "paseo.json" ||
				path === "AGENTS.md",
		)
		.filter((path) => {
			let stat;
			try {
				stat = lstatSync(join(root, path));
			} catch (error) {
				// Deleted tracked file: its absence already changes the path list.
				if ((error as NodeJS.ErrnoException).code === "ENOENT") return false;
				throw error;
			}
			if (!stat.isFile())
				throw new Error(`fingerprint input is not a regular file: ${path}`);
			return true;
		})
		.sort();
}
export function computeFingerprint(root: string): Fingerprint {
	const files = filesForFingerprint(root).map((path) => ({
		path,
		sha256: sha256(readFileSync(join(root, path))),
	}));
	const fingerprint = sha256(
		files.map((file) => `${file.path}\t${file.sha256}\n`).join(""),
	);
	return { fingerprint, files };
}
function parseJson<T>(text: string, label: string): T {
	try {
		return JSON.parse(text) as T;
	} catch (error) {
		throw new Error(
			`${label}: ${error instanceof Error ? error.message : String(error)}`,
		);
	}
}

function readManifest(dir: string): Manifest {
	const path = join(dir, "manifest.json");
	if (!existsSync(path)) throw new Error(`manifest not found: ${path}`);
	return parseJson<Manifest>(readFileSync(path, "utf8"), path);
}
function saveManifest(dir: string, manifest: Manifest): void {
	writeFileSync(join(dir, "manifest.json"), json(manifest));
}
function addArtifact(
	dir: string,
	manifest: Manifest,
	source: string,
	kind: string,
	target: string,
): Artifact {
	// Every evidence source (screenshot, review, report) must be a real file, not a link.
	const sourceStat = lstatSync(source);
	if (!sourceStat.isFile())
		throw new Error(`artifact source is not a regular file: ${source}`);
	const separator = target.lastIndexOf("/");
	mkdirSync(join(dir, separator < 0 ? "." : target.slice(0, separator)), {
		recursive: true,
	});
	copyFileSync(source, join(dir, target));
	const file = join(dir, target);
	const artifact = {
		path: target,
		sha256: sha256(readFileSync(file)),
		bytes: statSync(file).size,
		kind,
	};
	manifest.artifacts = manifest.artifacts.filter(
		(item) => item.path !== target,
	);
	manifest.artifacts.push(artifact);
	return artifact;
}
function parseArgs(args: string[]): Record<string, string> {
	const out: Record<string, string> = {};
	for (let index = 0; index < args.length; index += 1) {
		const arg = args[index];
		if (arg.startsWith("--")) {
			const value = args[index + 1];
			if (!value || value.startsWith("--"))
				throw new Error(`${arg} needs a value`);
			out[arg.slice(2)] = value;
			index += 1;
		}
	}
	return out;
}
function evidenceDir(root: string, value: string): string {
	const realRoot = realpathSync(root);
	const dir = realpathSync(resolve(root, value));
	const evidenceRoot = join(realRoot, ".harness", "evidence");
	if (!dir.startsWith(evidenceRoot + "/") && dir !== evidenceRoot) {
		throw new Error(
			`evidence directory must be under .harness/evidence: ${value}`,
		);
	}
	return dir;
}
function environment(root: string): {
	values: Record<string, unknown>;
	errors: string[];
} {
	const errors: string[] = [];
	const values: Record<string, unknown> = {};
	const ueRoot = process.env.UE_ROOT || "/Users/Shared/Epic Games/UE_5.8";
	const engineFile = join(ueRoot, "Engine/Build/Build.version");
	try {
		values.engineVersion = JSON.parse(readFileSync(engineFile, "utf8"));
	} catch (error) {
		const message = `UE_ROOT ${engineFile}: ${error instanceof Error ? error.message : String(error)}`;
		values.engineVersion = { status: "BLOCKED", error: message };
		errors.push(message);
	}
	for (const [key, argv] of Object.entries({
		xcode: ["xcodebuild", "-version"],
		clang: ["clang", "--version"],
		bun: ["bun", "--version"],
	})) {
		const result = command(argv, root);
		if (result.code !== 0) {
			const message = `${argv[0]}: ${result.error || result.stderr.trim() || `exit ${result.code}`}`;
			values[key] = { status: "BLOCKED", error: message };
			errors.push(message);
		} else {
			values[key] =
				key === "clang"
					? result.stdout.split(/\r?\n/, 1)[0]
					: result.stdout.trim();
		}
	}
	return { values, errors };
}
function init(root: string, args: string[]): number {
	const options = parseArgs(args);
	if (!options.run || !options.task)
		throw new Error("init requires --run <id> --task <taskId>");
	if (!/^[A-Za-z0-9._-]+$/.test(options.run))
		throw new Error("run id contains unsafe characters");
	const dir = join(root, ".harness/evidence", options.run);
	if (existsSync(join(dir, "manifest.json")))
		throw new Error(`refusing existing evidence run: ${dir}`);
	mkdirSync(dir, { recursive: true });
	const fingerprint = computeFingerprint(root);
	writeFileSync(join(dir, "fingerprint.json"), json(fingerprint));
	const env = environment(root);
	const manifest: Manifest = {
		runId: options.run,
		taskId: options.task,
		createdAt: now(),
		gitHead: git(root, ["rev-parse", "HEAD"]),
		gitStatus: run(["git", "status", "--porcelain"], root).stdout,
		sourceFingerprint: fingerprint.fingerprint,
		artifacts: [],
		commands: [],
		status: env.errors.length ? "BLOCKED" : "MANUAL_PENDING",
		blockers: env.errors,
		gates: [],
		screenshots: [],
		reviews: [],
		manual: [],
	};
	Object.assign(manifest, {
		environment: env.values,
		engineVersion: env.values.engineVersion,
		xcodeVersion: env.values.xcode,
		clangVersion: env.values.clang,
		bunVersion: env.values.bun,
	});
	saveManifest(dir, manifest);
	console.log(
		json({
			directory: dir,
			status: manifest.status,
			blockers: manifest.blockers,
		}),
	);
	return env.errors.length ? 2 : 0;
}
function jsonFromOutput(text: string): Record<string, unknown> | undefined {
	const start = text.indexOf("{");
	const end = text.lastIndexOf("}");
	if (start < 0 || end < start) return undefined;
	try {
		return JSON.parse(text.slice(start, end + 1)) as Record<string, unknown>;
	} catch {
		return undefined;
	}
}
function reportFiles(root: string): string[] {
	const output: string[] = [];
	const base = join(root, "TestReports");
	if (!existsSync(base)) return output;
	const walk = (dir: string) => {
		for (const entry of readdirSync(dir, { withFileTypes: true })) {
			const path = join(dir, entry.name);
			if (entry.isDirectory()) walk(path);
			else if (entry.isFile() && entry.name === "index.json") output.push(path);
		}
	};
	walk(base);
	return output;
}
function requiredTests(root: string): string[] {
	const names = new Set<string>();
	for (const path of filesForFingerprint(root).filter((file) =>
		file.startsWith("Source/"),
	)) {
		const text = readFileSync(join(root, path), "utf8");
		for (const match of text.matchAll(/["'](PSL\.[^"']+)["']/g))
			names.add(match[1]);
	}
	return [...names].sort();
}
function reportTests(
	report: unknown,
): { name: string; state: string | null }[] {
	if (
		!report ||
		typeof report !== "object" ||
		!Array.isArray((report as { tests?: unknown }).tests)
	)
		return [];
	return (report as { tests: unknown[] }).tests
		.map((entry) => {
			if (!entry || typeof entry !== "object") return undefined;
			const item = entry as {
				testDisplayName?: unknown;
				name?: unknown;
				fullTestPath?: unknown;
				state?: unknown;
			};
			// UE reports put the short leaf in testDisplayName; only fullTestPath carries "PSL.".
			const name = [item.fullTestPath, item.name, item.testDisplayName].find(
				(value) => typeof value === "string" && value,
			) as string | undefined;
			return name && (name === "PSL" || name.includes("PSL."))
				? { name, state: typeof item.state === "string" ? item.state : null }
				: undefined;
		})
		.filter((entry): entry is { name: string; state: string | null } =>
			Boolean(entry),
		);
}
function matchingState(
	entries: { name: string; state: string | null }[],
	required: string,
): string | null {
	return (
		entries.find(
			(entry) => entry.name === required || entry.name.endsWith(`.${required}`),
		)?.state ?? null
	);
}
function gate(root: string, dirArg: string, name: string): number {
	if (!["preflight", "build", "test"].includes(name))
		throw new Error(`unknown gate: ${name}`);
	const dir = evidenceDir(root, dirArg);
	const manifest = readManifest(dir);
	const before = computeFingerprint(root);
	if (before.fingerprint !== manifest.sourceFingerprint) {
		manifest.status = "STALE";
		saveManifest(dir, manifest);
		console.error("evidence fingerprint mismatch; gate refused (STALE)");
		return 2;
	}
	// Test-only seam: production always invokes the harness gate directly.
	const override = process.env.EVIDENCE_GATE_CMD;
	const argv = override
		? ["sh", "-c", `${override} ${name}`]
		: ["bun", "harness/src/cli.ts", "gate", name];
	const result = command(argv, root);
	const gateResult = jsonFromOutput(result.stdout);
	const record: Record<string, unknown> = {
		name,
		status:
			result.code === null ? "BLOCKED" : result.code === 0 ? "BLOCKED" : "FAIL",
		exitCode: result.code,
		startedAt: result.startedAt,
		endedAt: result.endedAt,
		argv,
		stdout: result.stdout,
		stderr: result.stderr,
		fingerprint: before.fingerprint,
	};
	manifest.commands.push({
		argv,
		exitCode: result.code,
		startedAt: result.startedAt,
		endedAt: result.endedAt,
	});
	if (result.error) record.error = result.error;
	let realLog: string | undefined;
	if (gateResult) {
		record.gateResult = gateResult;
		const logPath =
			typeof gateResult.logPath === "string" ? gateResult.logPath : undefined;
		const logs = join(realpathSync(root), ".harness", "logs") + "/";
		realLog =
			logPath && existsSync(resolve(root, logPath))
				? realpathSync(resolve(root, logPath))
				: undefined;
		if (realLog?.startsWith(logs)) {
			const target = `gates/${name}.log`;
			addArtifact(dir, manifest, realLog, "gate-log", target);
			record.logPath = target;
		} else if (logPath) {
			record.logError = realLog
				? `gate log outside .harness/logs: ${logPath}`
				: `gate log not found: ${logPath}`;
		}
	}
	if (name === "test") {
		const required = requiredTests(root);
		const startedMs = Date.parse(result.startedAt);
		const endedMs = Date.parse(result.endedAt);
		// Bound both ends: a pre-existing or future-dated report is not this gate's output.
		// +1 s slack: endedAt is ms-truncated ISO while APFS mtime is sub-ms.
		const freshReports = reportFiles(root)
			.filter((path) => {
				const mtime = statSync(path).mtimeMs;
				return mtime > startedMs && mtime <= endedMs + 1000;
			})
			.map((path) => realpathSync(path));
		// Bind to the report this gate's own contained log names (run_tests.sh PSL_REPORT_PATH),
		// so an unrelated report written in the same window cannot be picked up.
		const named =
			realLog && !record.logError
				? /^PSL_REPORT_PATH=(.+)$/m.exec(readFileSync(realLog, "utf8"))?.[1]
				: undefined;
		const namedReal =
			named && existsSync(named) ? realpathSync(named) : undefined;
		const fresh =
			namedReal && freshReports.includes(namedReal) ? namedReal : undefined;
		record.tests = required.map((testName) => ({
			name: testName,
			state: null,
		}));
		if (!fresh) {
			record.status =
				result.code === 2 || result.code === null ? "BLOCKED" : "FAIL";
			record.reportError = !named
				? "gate log names no PSL_REPORT_PATH"
				: `named report is missing or not created by this gate: ${named}`;
		} else {
			const target = `reports/test-${Date.now()}.json`;
			addArtifact(dir, manifest, fresh, "test-report", target);
			record.reportPath = target;
			const verify = command(
				[
					"python3",
					"scripts/verify_test_report.py",
					join(dir, target),
					"Source",
				],
				root,
			);
			record.reportVerify = {
				exitCode: verify.code,
				stdout: verify.stdout,
				stderr: verify.stderr,
			};
			try {
				const report = parseJson<unknown>(
					readFileSync(fresh, "utf8").replace(/^\uFEFF/, ""),
					fresh,
				);
				const entries = reportTests(report);
				record.tests = required.map((testName) => ({
					name: testName,
					state: matchingState(entries, testName),
				}));
			} catch (error) {
				record.reportError =
					error instanceof Error ? error.message : String(error);
			}
			if (
				result.code === 0 &&
				gateResult?.outcome === "SUCCESS" &&
				verify.code === 0 &&
				!record.reportError &&
				!record.logError
			)
				record.status = "AUTOMATED_PASS";
			else if (result.code === 2 || result.code === null)
				record.status = "BLOCKED";
			else record.status = "FAIL";
		}
	} else if (
		result.code === 0 &&
		gateResult?.outcome === "SUCCESS" &&
		!record.logError
	) {
		record.status = "AUTOMATED_PASS";
	} else if (result.code === 0) {
		record.status = "FAIL";
		record.error = record.logError
			? String(record.logError)
			: "gate exited 0 without a SUCCESS GateResult JSON";
	} else if (result.code === 2 || result.code === null) {
		record.status = "BLOCKED";
	}
	// Lock contention means the gate never ran: not a code failure.
	const failure = gateResult?.failure as { cls?: unknown } | undefined;
	if (failure?.cls === "BUILD_LOCKED") record.status = "BLOCKED";
	const after = computeFingerprint(root);
	record.fingerprintAfter = after.fingerprint;
	if (after.fingerprint !== before.fingerprint) record.status = "STALE";
	manifest.gates.push(record);
	manifest.status = record.status as EvidenceStatus;
	saveManifest(dir, manifest);
	console.log(json(record));
	return record.status === "AUTOMATED_PASS"
		? 0
		: record.status === "BLOCKED" || record.status === "STALE"
			? 2
			: 1;
}
function addScreenshot(root: string, dirArg: string, args: string[]): number {
	const sourceIndex = args.indexOf("--source");
	if (sourceIndex < 1 || !args[sourceIndex + 1])
		throw new Error("add-screenshot requires <dir> <png> --source <text>");
	const dir = evidenceDir(root, dirArg);
	const manifest = readManifest(dir);
	const input = resolve(root, args[0]);
	const bytes = readFileSync(input);
	if (
		bytes.length < 24 ||
		bytes.subarray(0, 8).toString("hex") !== "89504e470d0a1a0a" ||
		bytes.readUInt32BE(8) !== 13 ||
		bytes.subarray(12, 16).toString("ascii") !== "IHDR"
	)
		throw new Error("not a valid PNG");
	// Walk every chunk: lengths in bounds, CRCs valid, IDAT present, IEND last.
	let offset = 8;
	let sawIdat = false;
	let type = "";
	while (type !== "IEND") {
		if (offset + 12 > bytes.length)
			throw new Error("not a valid PNG: truncated");
		const length = bytes.readUInt32BE(offset);
		const end = offset + 12 + length;
		if (end > bytes.length)
			throw new Error("not a valid PNG: chunk overruns file");
		type = bytes.subarray(offset + 4, offset + 8).toString("ascii");
		const crc = Bun.hash.crc32(bytes.subarray(offset + 4, end - 4)) >>> 0;
		if (crc !== bytes.readUInt32BE(end - 4))
			throw new Error(`not a valid PNG: bad CRC in ${type}`);
		if (type === "IDAT") sawIdat = true;
		offset = end;
	}
	if (!sawIdat || offset !== bytes.length)
		throw new Error("not a valid PNG: missing IDAT or data after IEND");
	const width = bytes.readUInt32BE(16);
	const height = bytes.readUInt32BE(20);
	const target = `screenshots/${basename(input)}`;
	const artifact = addArtifact(dir, manifest, input, "screenshot", target);
	const capture = {
		path: target,
		...artifact,
		source: args.slice(sourceIndex + 1).join(" "),
		capturedAt: now(),
		fingerprint: computeFingerprint(root).fingerprint,
		width,
		height,
	};
	manifest.screenshots.push(capture);
	manifest.status = "VISUAL_VERIFIED";
	saveManifest(dir, manifest);
	console.log(json(capture));
	return 0;
}
function addReview(root: string, dirArg: string, reviewPath: string): number {
	const dir = evidenceDir(root, dirArg);
	const manifest = readManifest(dir);
	const review = parseJson<{
		verdict?: unknown;
		findings?: unknown;
		reviewedFingerprint?: unknown;
	}>(readFileSync(resolve(root, reviewPath), "utf8"), reviewPath);
	const dispositions = [
		"fixed",
		"rejected-with-evidence",
		"deferred-with-local-artifact",
		"blocked",
	];
	if (
		!["approve", "reject"].includes(String(review.verdict)) ||
		!Array.isArray(review.findings) ||
		typeof review.reviewedFingerprint !== "string"
	)
		throw new Error(
			"review must contain verdict, findings[], and reviewedFingerprint",
		);
	for (const finding of review.findings as Record<string, unknown>[]) {
		if (
			typeof finding?.id !== "string" ||
			!dispositions.includes(String(finding.disposition)) ||
			typeof finding.evidence !== "string" ||
			!finding.evidence
		)
			throw new Error(
				`every finding needs id, evidence, and disposition in ${dispositions.join("|")}`,
			);
	}
	const fingerprint = computeFingerprint(root).fingerprint;
	if (review.reviewedFingerprint !== fingerprint) {
		console.error(
			`review fingerprint mismatch: reviewed ${review.reviewedFingerprint}, current ${fingerprint}`,
		);
		return 2;
	}
	const target = `reviews/review-${Date.now()}.json`;
	const artifact = addArtifact(
		dir,
		manifest,
		resolve(root, reviewPath),
		"review",
		target,
	);
	manifest.reviews.push({
		...review,
		...artifact,
		fingerprint,
		recordedAt: now(),
	});
	manifest.status = review.verdict === "approve" ? "MANUAL_PASS" : "FAIL";
	saveManifest(dir, manifest);
	console.log(json(manifest.reviews.at(-1)));
	return review.verdict === "approve" ? 0 : 1;
}
function manual(root: string, dirArg: string, args: string[]): number {
	const [criterion, status, ...rest] = args;
	if (!criterion || !["MANUAL_PASS", "FAIL"].includes(status))
		throw new Error("manual requires <criterion> <MANUAL_PASS|FAIL>");
	const options = parseArgs(rest);
	if (!options.by || !options.note)
		throw new Error("manual requires --by <name> --note <text>");
	const observations: Record<string, unknown> = {};
	if (criterion === "A7" && status === "MANUAL_PASS") {
		// Contract A7: every PIE observation, and timings near 5 s drain / 8.3 s regen.
		const required = ["wasd", "mouse", "sprint", "release", "bars"];
		const checks = (options.checks || "").split(",");
		const missing = required.filter((check) => !checks.includes(check));
		const drain = Number(options["drain-s"]);
		const regen = Number(options["regen-s"]);
		if (
			options.map !== "L_TestArena" ||
			missing.length ||
			!(Math.abs(drain - 5) <= 1.5) ||
			!(Math.abs(regen - 8.3) <= 2.5)
		)
			throw new Error(
				`A7 MANUAL_PASS needs --map L_TestArena --checks ${required.join(",")} --drain-s ~5 --regen-s ~8.3 (missing: ${missing.join(",") || "none"})`,
			);
		Object.assign(observations, {
			map: options.map,
			checks: required,
			drainSeconds: drain,
			regenSeconds: regen,
		});
	}
	const dir = evidenceDir(root, dirArg);
	const manifest = readManifest(dir);
	const entry = {
		criterion,
		status,
		by: options.by,
		note: options.note,
		...observations,
		recordedAt: now(),
		fingerprint: computeFingerprint(root).fingerprint,
	};
	manifest.manual.push(entry);
	manifest.status = status as EvidenceStatus;
	saveManifest(dir, manifest);
	console.log(json(entry));
	return status === "FAIL" ? 1 : 0;
}
function freshRecord(
	record: Record<string, unknown>,
	fingerprint: string,
): boolean {
	return (
		record.fingerprint === fingerprint ||
		record.fingerprintAfter === fingerprint
	);
}
function verify(root: string, dirArg: string): number {
	const dir = evidenceDir(root, dirArg);
	const manifest = readManifest(dir);
	const current = computeFingerprint(root);
	const globalStale = current.fingerprint !== manifest.sourceFingerprint;
	const artifactErrors: string[] = [];
	// Hash and later parse the same bytes so a swap between reads cannot pass.
	const verifiedBytes = new Map<string, Buffer>();
	for (const artifact of manifest.artifacts) {
		const path = join(dir, artifact.path);
		if (!existsSync(path)) {
			artifactErrors.push(`${artifact.path}: missing`);
			continue;
		}
		const bytes = readFileSync(path);
		if (bytes.length !== artifact.bytes || sha256(bytes) !== artifact.sha256)
			artifactErrors.push(`${artifact.path}: sha256/size mismatch`);
		else verifiedBytes.set(artifact.path, bytes);
	}
	const stale = globalStale || artifactErrors.length > 0;
	const gates = manifest.gates.filter((record) =>
		freshRecord(record, current.fingerprint),
	);
	const latest = (name: string) =>
		[...gates].reverse().find((record) => record.name === name);
	const testGate = latest("test");
	const preflightGate = latest("preflight");
	const builtGate = latest("build");
	// Contract A1: preflight must exit 0 (20 GB floor) before a build counts.
	const buildGate =
		builtGate?.status === "AUTOMATED_PASS" &&
		preflightGate?.status !== "AUTOMATED_PASS"
			? {
					...builtGate,
					status: preflightGate ? preflightGate.status : "BLOCKED",
					missingPreflight: true,
				}
			: builtGate;
	const reportBytes =
		testGate && typeof testGate.reportPath === "string"
			? verifiedBytes.get(testGate.reportPath)
			: undefined;
	const testReport = reportBytes
		? parseJson<unknown>(
				reportBytes.toString("utf8").replace(/^\uFEFF/, ""),
				String(testGate?.reportPath),
			)
		: undefined;
	const entries = reportTests(testReport);
	const criterion = (id: string, status: EvidenceStatus, reason: string) => ({
		id,
		status,
		reason,
	});
	const automated = (
		name: string,
	): { status: EvidenceStatus; reason: string } => {
		if (!testGate) return { status: "BLOCKED", reason: "no fresh test gate" };
		if (testGate.status !== "AUTOMATED_PASS") {
			const status =
				testGate.status === "STALE"
					? "STALE"
					: testGate.status === "BLOCKED"
						? "BLOCKED"
						: "FAIL";
			return { status, reason: `test gate ${testGate.status}` };
		}
		const state = matchingState(entries, name);
		if (state === "Success")
			return { status: "AUTOMATED_PASS", reason: `${name} Success` };
		if (state === null)
			return { status: "FAIL", reason: `required test missing: ${name}` };
		return { status: "FAIL", reason: `${name} state=${state}` };
	};
	let criteria = [
		criterion(
			"A1",
			buildGate?.status === "AUTOMATED_PASS"
				? "AUTOMATED_PASS"
				: buildGate?.status === "STALE"
					? "STALE"
					: buildGate
						? (buildGate.status as EvidenceStatus)
						: "BLOCKED",
			buildGate
				? buildGate.missingPreflight
					? `build passed but no passing preflight (${buildGate.status})`
					: String(buildGate.status)
				: "no fresh build gate",
		),
		criterion(
			"A2",
			testGate?.status === "AUTOMATED_PASS"
				? "AUTOMATED_PASS"
				: testGate?.status === "STALE"
					? "STALE"
					: testGate
						? (testGate.status as EvidenceStatus)
						: "BLOCKED",
			testGate ? String(testGate.status) : "no fresh test gate",
		),
	];
	for (const [id, testName] of [
		["A3", "PSL.Input.DefaultBindings"],
		["A4", "PSL.Character.SprintSpeed"],
		["A5", "PSL.Hud.BarFraction"],
	] as const) {
		const result = automated(testName);
		criteria.push(criterion(id, result.status, result.reason));
	}
	const manualFor = (id: string) =>
		[...manifest.manual]
			.reverse()
			.find(
				(entry) =>
					entry.criterion === id && entry.fingerprint === current.fingerprint,
			);
	const a6 = manualFor("A6");
	const a7 = manualFor("A7");
	const reviewer = [...manifest.reviews]
		.reverse()
		.find((entry) => entry.fingerprint === current.fingerprint);
	criteria.push(
		criterion(
			"A6",
			a6 ? (a6.status as EvidenceStatus) : "MANUAL_PENDING",
			a6 ? String(a6.status) : "human Editor open verification pending",
		),
	);
	criteria.push(
		criterion(
			"A7",
			a7 ? (a7.status as EvidenceStatus) : "MANUAL_PENDING",
			a7 ? String(a7.status) : "PIE human verification pending",
		),
	);
	criteria.push(
		criterion(
			"A8",
			reviewer
				? reviewer.verdict === "approve"
					? "MANUAL_PASS"
					: "FAIL"
				: "BLOCKED",
			reviewer ? String(reviewer.verdict) : "no independent review",
		),
	);
	if (stale)
		criteria = criteria.map((item) => ({
			...item,
			status: "STALE",
			reason: globalStale
				? "source fingerprint changed"
				: artifactErrors.join("; "),
		}));
	const statuses = criteria.map((item) => item.status);
	const status: EvidenceStatus = statuses.includes("STALE")
		? "STALE"
		: statuses.includes("FAIL")
			? "FAIL"
			: statuses.includes("BLOCKED")
				? "BLOCKED"
				: statuses.includes("MANUAL_PENDING")
					? "MANUAL_PENDING"
					: statuses.includes("VISUAL_VERIFIED")
						? "VISUAL_VERIFIED"
						: statuses.includes("MANUAL_PASS")
							? "MANUAL_PASS"
							: "AUTOMATED_PASS";
	const result = {
		verifiedAt: now(),
		fingerprint: current.fingerprint,
		status,
		artifactErrors,
		criteria,
	};
	writeFileSync(join(dir, "verify.json"), json(result));
	console.log(json(result));
	return status === "STALE" || status === "FAIL"
		? 1
		: status === "BLOCKED"
			? 2
			: 0;
}
function show(root: string, dirArg: string): number {
	const dir = evidenceDir(root, dirArg);
	const manifest = readManifest(dir);
	const verification = existsSync(join(dir, "verify.json"))
		? parseJson<{
				status?: string;
				fingerprint?: string;
				criteria?: { id: string; status: string }[];
			}>(readFileSync(join(dir, "verify.json"), "utf8"), "verify.json")
		: undefined;
	// show is a summary, not a verifier: a cached verdict for another tree is STALE.
	const current =
		verification?.fingerprint === computeFingerprint(root).fingerprint;
	const status = !verification
		? "UNVERIFIED"
		: current
			? verification.status
			: "STALE";
	console.log(
		json({
			runId: manifest.runId,
			taskId: manifest.taskId,
			status,
			gates: manifest.gates.map((gate) => ({
				name: gate.name,
				status: gate.status,
			})),
			criteria: current ? verification?.criteria : undefined,
		}),
	);
	return current && verification?.status !== "FAIL" ? 0 : 1;
}
export async function main(argv = process.argv.slice(2)): Promise<number> {
	try {
		const root = repoRoot();
		const [subcommand, ...args] = argv;
		switch (subcommand) {
			case "init":
				return init(root, args);
			case "gate":
				return gate(root, args[0], args[1]);
			case "add-screenshot":
				return addScreenshot(root, args[0], args.slice(1));
			case "add-review":
				return addReview(root, args[0], args[1]);
			case "manual":
				return manual(root, args[0], args.slice(1));
			case "verify":
				return verify(root, args[0]);
			case "show":
				return show(root, args[0]);
			default:
				throw new Error(
					"usage: bun scripts/evidence.ts <init|gate|add-screenshot|add-review|manual|verify|show>",
				);
		}
	} catch (error) {
		console.error(error instanceof Error ? error.message : String(error));
		return 1;
	}
}

if (import.meta.main) process.exitCode = await main();
