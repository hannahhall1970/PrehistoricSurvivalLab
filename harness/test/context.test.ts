import { describe, expect, it } from "bun:test";
import {
	mkdirSync,
	mkdtempSync,
	readFileSync,
	rmSync,
	writeFileSync,
} from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { RepoContextBuilder } from "../src/context.ts";
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

describe("RepoContextBuilder", () => {
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
});
