import { describe, expect, it } from "bun:test";
import {
	mkdirSync,
	mkdtempSync,
	readFileSync,
	rmSync,
	symlinkSync,
	writeFileSync,
} from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { RepoContextBuilder, redactSecrets } from "../src/context.ts";
import type { RunState, TaskState } from "../src/types.ts";

const makeRepo = (): string => {
	const root = mkdtempSync(join(tmpdir(), "psl-context-"));
	mkdirSync(join(root, "Source", "Demo", "Public"), { recursive: true });
	writeFileSync(
		join(root, "AGENTS.md"),
		"Project rules\nDo not fabricate results.\n",
	);
	writeFileSync(
		join(root, "Source", "Demo", "Public", "DemoHUD.h"),
		"UCLASS()\nclass ADemoHUD {\nGENERATED_BODY()\npublic:\n UFUNCTION()\n void DrawHUD();\n};\n",
	);
	writeFileSync(
		join(root, "Source", "Demo", "Private.cpp"),
		"int demo() { return 1; }\n",
	);
	Bun.spawnSync(["git", "init", "-q"], { cwd: root });
	return root;
};

const run: RunState = {
	id: "run-1",
	objective: "Implement HUD",
	stage: "EXECUTE",
	createdAt: new Date().toISOString(),
	updatedAt: new Date().toISOString(),
	stopRequested: false,
	version: 1,
};
const task: TaskState = {
	id: "hud",
	title: "HUD",
	description: "Draw values",
	dependsOn: [],
	acceptance: ["Draw values"],
	files: ["Source/Demo/Public/DemoHUD.h", "Source/Demo/Private.cpp"],
	needsBuild: true,
	maxAttempts: 2,
	budgetMs: 1000,
	status: "RUNNING",
	attempt: 1,
	lastFailure: {
		cls: "COMPILE_ERROR",
		summary: "compile failed",
		excerpt: "PSLHUD.cpp:35:13: error: missing member",
		logPath: "/tmp/full.log",
	},
};

const makeSecurityRepo = (): { root: string; outside: string } => {
	const root = mkdtempSync(join(tmpdir(), "psl-context-secure-"));
	const outside = mkdtempSync(join(tmpdir(), "psl-context-outside-"));
	mkdirSync(join(root, "Config"), { recursive: true });
	mkdirSync(join(root, "Source"), { recursive: true });
	mkdirSync(join(root, "keys"), { recursive: true });
	writeFileSync(
		join(root, "Config", "DefaultEngine.ini"),
		"[Security]\nSecurityToken=FAKE0123456789ABCDEF0123456789AB\n",
	);
	writeFileSync(join(root, "Source", "Tracked.cpp"), "int value = 1;\n");
	writeFileSync(join(root, ".gitignore"), ".env\n");
	writeFileSync(
		join(root, ".env"),
		["API_KEY=", "fake-ignored-env-value\n"].join(""),
	);
	writeFileSync(
		join(root, ".env.local"),
		["API_KEY=", "fake-local-env-value\n"].join(""),
	);
	writeFileSync(join(root, "keys", "dev.pem"), "fake-pem-value\n");
	mkdirSync(join(root, "secrets"), { recursive: true });
	writeFileSync(join(root, "secrets", "oauth.json"), "fake-oauth-secret\n");
	mkdirSync(join(root, "config", "credentials"), { recursive: true });
	writeFileSync(
		join(root, "config", "credentials", "prod.json"),
		"fake-prod-credential\n",
	);
	writeFileSync(join(outside, "outside.h"), "fake-outside-marker\n");
	symlinkSync(join(outside, "outside.h"), join(root, "Source", "link.h"));
	Bun.spawnSync(["git", "init", "-q"], { cwd: root });
	Bun.spawnSync(["git", "config", "user.email", "test@example.invalid"], {
		cwd: root,
	});
	Bun.spawnSync(["git", "config", "user.name", "Context Test"], { cwd: root });
	Bun.spawnSync(
		[
			"git",
			"add",
			".gitignore",
			"Config/DefaultEngine.ini",
			"Source/Tracked.cpp",
			"keys/dev.pem",
		],
		{ cwd: root },
	);
	Bun.spawnSync(["git", "commit", "-qm", "base"], { cwd: root });
	writeFileSync(
		join(root, "Source", "Tracked.cpp"),
		'int value = 1;\npassword = "fake-pass-123";\n',
	);
	return { root, outside };
};

describe("RepoContextBuilder", () => {
	it("rejects an untrusted persisted worktree before reading its files", async () => {
		const root = makeRepo();
		const outside = mkdtempSync(join(tmpdir(), "psl-context-outside-wt-"));
		const id = crypto.randomUUID();
		const builder = new RepoContextBuilder({
			repoRoot: root,
			artifactDir: join(root, ".harness"),
		});
		const forged = {
			...run,
			id,
			worktreePath: outside,
			worktreeBranch: `harness/${id}`,
		};
		await expect(builder.forTask(forged, task, "implementer")).rejects.toThrow(
			"worktree validation failed",
		);
		rmSync(root, { recursive: true, force: true });
		rmSync(outside, { recursive: true, force: true });
	});

	it("includes contract, signatures, git state, and only failure excerpts", async () => {
		const root = makeRepo();
		const builder = new RepoContextBuilder({
			repoRoot: root,
			artifactDir: join(root, ".harness"),
		});
		const prompt = await builder.forTask(run, task, "implementer");
		expect(prompt).toContain("Task contract");
		expect(prompt).toContain("UCLASS");
		expect(prompt).toContain("DrawHUD");
		expect(prompt).toContain("PSLHUD.cpp:35:13");
		expect(prompt).toContain("full.log");
		rmSync(root, { recursive: true, force: true });
	});

	it("gives the reviewer the diff and gate evidence, but not the implementer", async () => {
		const root = makeRepo();
		Bun.spawnSync(
			["git", "-c", "user.email=t@t", "-c", "user.name=t", "add", "."],
			{ cwd: root },
		);
		Bun.spawnSync(
			[
				"git",
				"-c",
				"user.email=t@t",
				"-c",
				"user.name=t",
				"commit",
				"-qm",
				"base",
			],
			{ cwd: root },
		);
		writeFileSync(
			join(root, "Source", "Demo", "Private.cpp"),
			"int demo() { return 2; }\n",
		);
		const builder = new RepoContextBuilder({
			repoRoot: root,
			artifactDir: join(root, ".harness"),
		});
		const review = await builder.forTask(run, task, "reviewer", [
			"BUILD SUCCESS exit=0 log=/tmp/b.log",
		]);
		expect(review).toContain("+int demo() { return 2; }");
		expect(review).toContain("BUILD SUCCESS exit=0");
		const implement = await builder.forTask(run, task, "implementer");
		expect(implement).not.toContain("## Git diff\n");
		rmSync(root, { recursive: true, force: true });
	});

	it("respects the total context byte budget", async () => {
		const root = makeRepo();
		const builder = new RepoContextBuilder({
			repoRoot: root,
			artifactDir: join(root, ".harness"),
			maxTotalBytes: 200,
		});
		const prompt = await builder.forTask(run, task, "reviewer");
		expect(Buffer.byteLength(prompt)).toBeLessThanOrEqual(220);
		expect(prompt).toContain("context truncated");
		rmSync(root, { recursive: true, force: true });
	});

	it("writes durable role handoffs", () => {
		const root = makeRepo();
		const artifactDir = join(root, ".harness");
		const builder = new RepoContextBuilder({ repoRoot: root, artifactDir });
		const path = builder.writeHandoff(run, task, {
			role: "implementer",
			summary: "implemented",
			agentId: "agent-1",
		});
		expect(readFileSync(path, "utf8")).toContain("implemented");
		rmSync(root, { recursive: true, force: true });
	});
	it("ignores symlinked previous handoffs", async () => {
		const root = makeRepo();
		const artifactDir = mkdtempSync(join(tmpdir(), "psl-context-artifacts-"));
		const handoffDir = join(artifactDir, "runs", run.id, task.id);
		const outside = mkdtempSync(join(tmpdir(), "psl-context-handoff-outside-"));
		const marker = "fake-external-handoff-marker";
		mkdirSync(handoffDir, { recursive: true });
		const outsideFile = join(outside, "handoff.md");
		writeFileSync(outsideFile, marker);
		symlinkSync(outsideFile, join(handoffDir, "handoff-9-implementer.md"));
		const builder = new RepoContextBuilder({ repoRoot: root, artifactDir });
		const prompt = await builder.forTask(run, task, "implementer");
		expect(prompt).toContain("## Previous handoff\n(none)");
		expect(prompt).not.toContain(marker);
		rmSync(root, { recursive: true, force: true });
		rmSync(artifactDir, { recursive: true, force: true });
		rmSync(outside, { recursive: true, force: true });
	});
	it("fails closed and redacts sensitive files and reviewer context", async () => {
		const { root, outside } = makeSecurityRepo();
		const builder = new RepoContextBuilder({
			repoRoot: root,
			artifactDir: join(root, ".harness"),
		});
		const secureTask = { ...task, files: ["**"] };
		const prompt = await builder.forTask(run, secureTask, "reviewer");
		expect(prompt).toContain("SecurityToken=[REDACTED]");
		expect(prompt).toContain("### .env.local\n(excluded: sensitive path)");
		expect(prompt).toContain("### keys/dev.pem\n(excluded: sensitive path)");
		expect(prompt).toContain(
			"### secrets/oauth.json\n(excluded: sensitive path)",
		);
		expect(prompt).toMatch(
			/### [Cc]onfig\/credentials\/prod\.json\n\(excluded: sensitive path\)/,
		);
		expect(prompt).toContain(["pass", "word"].join("") + " = [REDACTED]");
		expect(prompt).not.toContain("FAKE0123456789ABCDEF0123456789AB");
		expect(prompt).not.toContain("fake-ignored-env-value");
		expect(prompt).not.toContain("fake-local-env-value");
		expect(prompt).not.toContain("fake-pem-value");
		expect(prompt).not.toContain("fake-oauth-secret");
		expect(prompt).not.toContain("fake-prod-credential");
		expect(prompt).not.toContain("fake-outside-marker");
		rmSync(root, { recursive: true, force: true });
		rmSync(outside, { recursive: true, force: true });
	});
	it("redacts quoted assignment keys and values", () => {
		expect(redactSecrets('{"SecurityToken": "abc"}')).toBe(
			'{"SecurityToken": [REDACTED]}',
		);
		expect(redactSecrets('{"api_key": "xyz"}')).toBe('{"api_key": [REDACTED]}');
		expect(redactSecrets(["pass", "word"].join("") + ': "hello world"')).toBe(
			["pass", "word"].join("") + ": [REDACTED]",
		);
	});

	it("reports unavailable task files outside a Git repository", async () => {
		const root = mkdtempSync(join(tmpdir(), "psl-context-non-git-"));
		writeFileSync(join(root, "tracked-looking.txt"), "not a repository\n");
		const builder = new RepoContextBuilder({
			repoRoot: root,
			artifactDir: join(root, ".harness"),
		});
		const prompt = await builder.forTask(run, task, "implementer");
		expect(prompt).toContain(
			"## Task files\n(unavailable: git ls-files failed)",
		);
		rmSync(root, { recursive: true, force: true });
	});

	it("redacts common token formats but leaves plain identifiers unchanged", () => {
		const privateKey = [
			"-----BEGIN ",
			"TEST PRIVATE KEY-----\nFAKEPRIVATEKEYDATA\n-----END ",
			"TEST PRIVATE KEY-----",
		].join("");
		const bearer = ["Bearer ", "fake-bearer-token"].join("");
		const github = ["ghp_", "12345678901234567890"].join("");
		const openAi = ["sk-", "12345678901234567890"].join("");
		const text = [privateKey, `Authorization: ${bearer}`, github, openAi].join(
			"\n",
		);
		const redacted = redactSecrets(text);
		expect(redacted).not.toContain("FAKEPRIVATEKEYDATA");
		expect(redacted).not.toContain("fake-bearer-token");
		expect(redacted).not.toContain(github);
		expect(redacted).not.toContain(openAi);
		expect(redactSecrets("UPROPERTY() float StaminaToken;")).toBe(
			"UPROPERTY() float StaminaToken;",
		);
	});
});
