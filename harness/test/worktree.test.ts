import { afterEach, describe, expect, it } from "bun:test";
import {
	existsSync,
	lstatSync,
	mkdtempSync,
	mkdirSync,
	readFileSync,
	realpathSync,
	rmSync,
	symlinkSync,
	writeFileSync,
} from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { RepoContextBuilder } from "../src/context.ts";
import { Runner } from "../src/graph.ts";
import { Store } from "../src/store.ts";
import { validateWorktree } from "../src/worktree.ts";
import type { ContextBuilder } from "../src/context.ts";
import type {
	AgentPort,
	AgentRequest,
	AgentResult,
	Failure,
	GateResult,
	HarnessConfig,
	Role,
	TaskSpec,
} from "../src/types.ts";
import type { Gates } from "../src/gates.ts";

const roots: string[] = [];
const stores: Store[] = [];

afterEach(() => {
	for (const store of stores.splice(0)) store.close();
	for (const root of roots.splice(0))
		rmSync(root, { recursive: true, force: true });
});

function command(cwd: string, args: string[]): string {
	const result = Bun.spawnSync(args, { cwd, stdout: "pipe", stderr: "pipe" });
	if (result.exitCode !== 0)
		throw new Error(
			`${args.join(" ")}: ${new TextDecoder().decode(result.stderr)}`,
		);
	return new TextDecoder().decode(result.stdout);
}

function makeRepo(): string {
	const root = mkdtempSync(join(tmpdir(), "psl-worktree-"));
	roots.push(root);
	const origin = join(root, "origin.git");
	const repo = join(root, "repo");
	command(root, ["git", "init", "--bare", "-q", origin]);
	command(root, ["git", "init", "-q", repo]);
	command(repo, ["git", "config", "user.email", "harness@example.test"]);
	command(repo, ["git", "config", "user.name", "Harness Test"]);
	writeFileSync(join(repo, "tracked.txt"), "base\n");
	writeFileSync(join(repo, ".gitignore"), ".harness/\n");
	command(repo, ["git", "add", "tracked.txt", ".gitignore"]);
	command(repo, ["git", "commit", "-qm", "base"]);
	command(repo, ["git", "branch", "-M", "main"]);
	command(repo, ["git", "remote", "add", "origin", origin]);
	command(repo, ["git", "push", "-q", "-u", "origin", "main"]);
	return repo;
}

function addWorktree(
	repo: string,
	id = crypto.randomUUID(),
): {
	id: string;
	path: string;
	branch: string;
} {
	const path = join(repo, ".harness", "worktrees", id);
	mkdirSync(join(repo, ".harness", "worktrees"), { recursive: true });
	const branch = `harness/${id}`;
	command(repo, [
		"git",
		"worktree",
		"add",
		"-q",
		"-b",
		branch,
		path,
		"origin/main",
	]);
	return { id, path, branch };
}

function initStandaloneRepo(path: string, branch: string): void {
	mkdirSync(path, { recursive: true });
	command(path, ["git", "init", "-q"]);
	command(path, ["git", "config", "user.email", "harness@example.test"]);
	command(path, ["git", "config", "user.name", "Harness Test"]);
	writeFileSync(join(path, "standalone.txt"), "standalone\n");
	command(path, ["git", "add", "standalone.txt"]);
	command(path, ["git", "commit", "-qm", "standalone"]);
	command(path, ["git", "branch", "-M", branch]);
}

function config(): HarnessConfig {
	return {
		daemonUrl: "ws://fake",
		roles: {
			planner: { provider: "fake", modeId: "full" },
			implementer: { provider: "fake", modeId: "full" },
			reviewer: { provider: "fake", modeId: "full" },
		},
		limits: {
			defaultMaxAttempts: 2,
			defaultBudgetMs: 1000,
			buildTimeoutMs: 1000,
			testTimeoutMs: 1000,
			preflightTimeoutMs: 1000,
			maxTasksPerRun: 4,
		},
		commands: { preflight: ["true"], build: ["true"], test: ["true"] },
		useWorktrees: true,
	};
}

function task(needsBuild = true): TaskSpec {
	return {
		id: "task-1",
		title: "Edit isolated file",
		description: "Make the requested edit in the worktree",
		dependsOn: [],
		acceptance: ["The worktree contains the edit"],
		files: ["tracked.txt"],
		needsBuild,
		maxAttempts: 2,
		budgetMs: 1000,
	};
}

function gate(gate: GateResult["gate"]): GateResult {
	return {
		gate,
		outcome: "SUCCESS",
		exitCode: 0,
		durationMs: 0,
		command: ["true"],
		logPath: `${gate}.log`,
	};
}

class FakeGates implements Gates {
	cwd: string[] = [];
	preflight(): Promise<GateResult> {
		return Promise.resolve(gate("PREFLIGHT"));
	}
	build(_signal?: AbortSignal, cwd?: string): Promise<GateResult> {
		if (cwd) this.cwd.push(cwd);
		return Promise.resolve(gate("BUILD"));
	}
	test(_signal?: AbortSignal, cwd?: string): Promise<GateResult> {
		if (cwd) this.cwd.push(cwd);
		return Promise.resolve(gate("TEST"));
	}
}

class FakeContext implements ContextBuilder {
	constructor(private readonly artifactDir: string) {}
	async forPlanner(): Promise<string> {
		return "planner";
	}
	async forTask(): Promise<string> {
		return "worker";
	}
	writeHandoff(
		_run: unknown,
		taskState: { id: string; attempt: number },
		data: {
			role: Role;
			summary: string;
			failure?: Failure;
			agentId?: string;
			evidence?: string[];
		},
	): string {
		const path = join(this.artifactDir, `${taskState.id}-${data.role}.md`);
		writeFileSync(
			path,
			`${data.summary}\n${data.evidence?.join("\n") ?? ""}\n`,
		);
		return path;
	}
}

class FakeAgents implements AgentPort {
	readonly createCount = new Map<string, number>();
	readonly cwd: string[] = [];
	private readonly created = new Set<string>();
	private crashed = false;
	constructor(
		private readonly writeEdit: boolean,
		private readonly crashFirst = false,
		private readonly commitEdit = false,
	) {}
	async run(
		request: AgentRequest,
		_signal?: AbortSignal,
		hooks?: { onCreated?(agentId: string): void },
	): Promise<AgentResult> {
		this.cwd.push(request.cwd);
		if (!this.created.has(request.idempotencyKey)) {
			this.created.add(request.idempotencyKey);
			this.createCount.set(request.idempotencyKey, 1);
			hooks?.onCreated?.(`agent-${this.created.size}`);
		}
		if (request.role === "implementer") {
			if (this.crashFirst && !this.crashed) {
				this.crashed = true;
				throw new Error("simulated crash");
			}
			if (this.writeEdit) {
				const path = this.commitEdit
					? join(request.cwd, "Source", "Committed.cpp")
					: join(request.cwd, "tracked.txt");
				if (this.commitEdit)
					mkdirSync(join(request.cwd, "Source"), { recursive: true });
				writeFileSync(path, this.commitEdit ? "committed\n" : "edited\n");
				if (this.commitEdit) {
					command(request.cwd, ["git", "add", "Source/Committed.cpp"]);
					command(request.cwd, ["git", "commit", "-qm", "worker edit"]);
				}
			}
			return { status: "idle", agentId: "implementer" };
		}
		if (request.role === "reviewer")
			return {
				status: "idle",
				agentId: "reviewer",
				structured: { verdict: "approve", findings: [] },
			};
		return {
			status: "idle",
			agentId: "planner",
			structured: { tasks: [task()] },
		};
	}
	async stop(): Promise<void> {}
}

function makeRunner(
	repoRoot: string,
	agents: AgentPort,
	gates: Gates,
): { runner: Runner; store: Store } {
	const store = new Store(join(repoRoot, ".harness", "state.db"));
	stores.push(store);
	return {
		store,
		runner: new Runner({
			store,
			agents,
			gates,
			context: new RepoContextBuilder({
				repoRoot,
				artifactDir: join(repoRoot, ".harness"),
			}),
			config: config(),
			repoRoot,
			owner: `test-${Math.random()}`,
		}),
	};
}

describe("validateWorktree", () => {
	it("accepts a registered worktree on the expected branch", () => {
		const repo = makeRepo();
		const worktree = addWorktree(repo);
		expect(
			validateWorktree(repo, worktree.id, worktree.path, worktree.branch),
		).toEqual({
			ok: true,
			path: realpathSync(worktree.path),
		});
	});
	it("ignores unrelated GIT_DIR and GIT_WORK_TREE overrides", () => {
		const repo = makeRepo();
		const unrelated = makeRepo();
		const worktree = addWorktree(repo);
		const forgedId = crypto.randomUUID();
		const forgedPath = join(repo, ".harness", "worktrees", forgedId);
		mkdirSync(join(repo, ".harness", "worktrees"), { recursive: true });
		command(repo, [
			"git",
			"clone",
			"-q",
			join(repo, "..", "origin.git"),
			forgedPath,
		]);

		const previousGitDir = process.env.GIT_DIR;
		const previousGitWorkTree = process.env.GIT_WORK_TREE;
		try {
			process.env.GIT_DIR = join(unrelated, ".git");
			process.env.GIT_WORK_TREE = unrelated;

			expect(
				validateWorktree(repo, worktree.id, worktree.path, worktree.branch),
			).toEqual({ ok: true, path: realpathSync(worktree.path) });
			const forged = validateWorktree(
				repo,
				forgedId,
				forgedPath,
				`harness/${forgedId}`,
			);
			expect(forged.ok).toBe(false);
			if (!forged.ok) expect(forged.reason).toContain("common-dir");
		} finally {
			if (previousGitDir === undefined) delete process.env.GIT_DIR;
			else process.env.GIT_DIR = previousGitDir;
			if (previousGitWorkTree === undefined) delete process.env.GIT_WORK_TREE;
			else process.env.GIT_WORK_TREE = previousGitWorkTree;
		}
	});

	it("rejects an unrelated Git repository at the expected path", () => {
		const repo = makeRepo();
		const id = crypto.randomUUID();
		const expected = join(repo, ".harness", "worktrees", id);
		initStandaloneRepo(expected, `harness/${id}`);
		const result = validateWorktree(repo, id, expected, `harness/${id}`);
		expect(result.ok).toBe(false);
		if (!result.ok) expect(result.reason).toContain("common-dir");
	});

	it("rejects an arbitrary non-Git directory", () => {
		const repo = makeRepo();
		const id = crypto.randomUUID();
		const expected = join(repo, ".harness", "worktrees", id);
		mkdirSync(expected, { recursive: true });
		const result = validateWorktree(repo, id, expected, `harness/${id}`);
		expect(result.ok).toBe(false);
	});

	it("rejects a deleted worktree", () => {
		const repo = makeRepo();
		const worktree = addWorktree(repo);
		rmSync(worktree.path, { recursive: true, force: true });
		const result = validateWorktree(
			repo,
			worktree.id,
			worktree.path,
			worktree.branch,
		);
		expect(result.ok).toBe(false);
	});

	it("rejects a worktree directory replaced by a symlink", () => {
		const repo = makeRepo();
		const outside = makeRepo();
		const outsideWorktree = addWorktree(outside);
		const id = crypto.randomUUID();
		const expected = join(repo, ".harness", "worktrees", id);
		mkdirSync(join(repo, ".harness", "worktrees"), { recursive: true });
		symlinkSync(outsideWorktree.path, expected);
		const result = validateWorktree(repo, id, expected, `harness/${id}`);
		expect(lstatSync(expected).isSymbolicLink()).toBe(true);
		expect(result.ok).toBe(false);
		if (!result.ok) expect(result.reason).toContain("symlink");
	});

	it("rejects a persisted branch that differs from the actual branch", () => {
		const repo = makeRepo();
		const worktree = addWorktree(repo);
		const result = validateWorktree(
			repo,
			worktree.id,
			worktree.path,
			"harness/not-the-run",
		);
		expect(result.ok).toBe(false);
		if (!result.ok) expect(result.reason).toContain("persisted branch");
	});

	it("rejects an unregistered clone at the expected path", () => {
		const repo = makeRepo();
		const id = crypto.randomUUID();
		const expected = join(repo, ".harness", "worktrees", id);
		mkdirSync(join(repo, ".harness", "worktrees"), { recursive: true });
		command(repo, [
			"git",
			"clone",
			"-q",
			join(repo, "..", "origin.git"),
			expected,
		]);
		const result = validateWorktree(repo, id, expected, `harness/${id}`);
		expect(result.ok).toBe(false);
		if (!result.ok) expect(result.reason).toContain("common-dir");
	});

	it("rejects run IDs that can escape the worktree root", () => {
		const repo = makeRepo();
		const result = validateWorktree(repo, "../outside", undefined, undefined);
		expect(result.ok).toBe(false);
		if (!result.ok) expect(result.reason).toContain("runId");
	});
});

describe("PSL worktree isolation", () => {
	it("creates an isolated branch, runs gates in it, records diff evidence, and keeps dirty work", async () => {
		const repo = makeRepo();
		const agents = new FakeAgents(true);
		const gates = new FakeGates();
		const { runner, store } = makeRunner(repo, agents, gates);
		const run = await runner.start("edit", { tasks: [task()] });
		expect(run.stage).toBe("DONE");
		expect(run.worktreeBranch).toBe(`harness/${run.id}`);
		expect(run.worktreePath).toBe(
			join(realpathSync(repo), ".harness", "worktrees", run.id),
		);
		expect(existsSync(run.worktreePath!)).toBe(true);
		expect(command(repo, ["git", "branch", "--show-current"]).trim()).toBe(
			"main",
		);
		expect(command(repo, ["git", "status", "--porcelain"]).trim()).toBe("");
		expect(gates.cwd).toEqual([run.worktreePath!, run.worktreePath!]);
		expect(
			readFileSync(
				join(
					repo,
					".harness",
					"runs",
					run.id,
					"task-1",
					"handoff-1-implementer.md",
				),
				"utf8",
			),
		).toContain("diff --stat origin/main");
		expect(
			store.events(run.id).find((event) => event.kind === "worktree_cleanup")
				?.data.removed,
		).toBe(false);
	});

	it("runs build and test when a worker commits build-relevant work", async () => {
		const repo = makeRepo();
		const agents = new FakeAgents(true, false, true);
		const gates = new FakeGates();
		const { runner } = makeRunner(repo, agents, gates);
		const docsOnlyTask = { ...task(false), files: ["docs/README.md"] };
		const run = await runner.start("committed source edit", {
			tasks: [docsOnlyTask],
		});
		expect(run.stage).toBe("DONE");
		expect(gates.cwd).toEqual([run.worktreePath!, run.worktreePath!]);
	});

	it("removes a clean worktree without force", async () => {
		const repo = makeRepo();
		const agents = new FakeAgents(false);
		const { runner } = makeRunner(repo, agents, new FakeGates());
		const run = await runner.start("no edit", { tasks: [task(false)] });
		expect(run.stage).toBe("DONE");
		expect(existsSync(run.worktreePath!)).toBe(false);
	});

	it("resumes after a crash with the same worktree and worker key", async () => {
		const repo = makeRepo();
		const agents = new FakeAgents(false, true);
		const first = makeRunner(repo, agents, new FakeGates());
		await expect(
			first.runner.start("crash", { tasks: [task(false)] }),
		).rejects.toThrow("simulated crash");
		const crashed = first.store.listRuns()[0]!;
		expect(crashed.worktreePath).toBe(
			join(realpathSync(repo), ".harness", "worktrees", crashed.id),
		);
		expect(existsSync(crashed.worktreePath!)).toBe(true);
		const second = makeRunner(repo, agents, new FakeGates());
		const done = await second.runner.resume(crashed.id);
		expect(done.stage).toBe("DONE");
		expect(done.worktreePath).toBe(crashed.worktreePath);
		expect(agents.createCount.get(`${crashed.id}:task-1:implementer:1`)).toBe(
			1,
		);
	});
	it("blocks a forged persisted worktree before agent or gate cwd use", async () => {
		const repo = makeRepo();
		const outside = makeRepo();
		const marker = join(outside, "outside-marker.txt");
		writeFileSync(marker, "must not be read\n");
		const agents = new FakeAgents(false, true);
		const gates = new FakeGates();
		const first = makeRunner(repo, agents, gates);
		await expect(
			first.runner.start("forge", { tasks: [task(false)] }),
		).rejects.toThrow("simulated crash");
		const crashed = first.store.listRuns()[0]!;
		crashed.worktreePath = realpathSync(outside);
		crashed.worktreeBranch = `harness/${crashed.id}`;
		first.store.saveRun(crashed);
		const blocked = await first.runner.resume(crashed.id);
		expect(blocked.stage).toBe("BLOCKED");
		expect(blocked.parkedReason).toContain("worktree validation failed");
		expect(blocked.parkedReason).toContain("expected");
		expect(agents.cwd).not.toContain(realpathSync(outside));
		expect(gates.cwd).toEqual([]);
	});
});

/** Local branch `wip` one commit ahead of origin/main, carrying Source/W1.cpp (stands in for the reviewed W1 commit). */
function addWipCommit(repo: string): string {
	command(repo, ["git", "checkout", "-q", "-b", "wip"]);
	mkdirSync(join(repo, "Source"), { recursive: true });
	writeFileSync(join(repo, "Source", "W1.cpp"), "w1\n");
	command(repo, ["git", "add", "Source/W1.cpp"]);
	command(repo, ["git", "commit", "-qm", "wip W1"]);
	const sha = command(repo, ["git", "rev-parse", "HEAD"]).trim();
	command(repo, ["git", "checkout", "-q", "main"]);
	return sha;
}

describe("explicit base commit", () => {
	it("defaults to origin/main and persists no base", async () => {
		const repo = makeRepo();
		const agents = new FakeAgents(false, true);
		const first = makeRunner(repo, agents, new FakeGates());
		await expect(
			first.runner.start("default", { tasks: [task(false)] }),
		).rejects.toThrow("simulated crash");
		const run = first.store.listRuns()[0]!;
		expect(run.baseSha).toBeUndefined();
		expect(command(run.worktreePath!, ["git", "rev-parse", "HEAD"])).toBe(
			command(repo, ["git", "rev-parse", "origin/main"]),
		);
	});

	it("branches the worktree from the resolved SHA, inherits its files, and keeps it across resume", async () => {
		const repo = makeRepo();
		const wip = addWipCommit(repo);
		const agents = new FakeAgents(false, true);
		const first = makeRunner(repo, agents, new FakeGates());
		await expect(
			first.runner.start("continue", {
				tasks: [task(false)],
				baseCommit: "wip",
			}),
		).rejects.toThrow("simulated crash");
		const crashed = first.store.listRuns()[0]!;
		expect(crashed.baseSha).toBe(wip);
		expect(crashed.baseSha).toMatch(/^[0-9a-f]{40}$/);
		expect(
			readFileSync(join(crashed.worktreePath!, "Source", "W1.cpp"), "utf8"),
		).toBe("w1\n");
		expect(
			command(crashed.worktreePath!, ["git", "rev-parse", "HEAD"]).trim(),
		).toBe(wip);
		expect(command(crashed.worktreePath!, ["git", "diff", "--stat", wip])).toBe(
			"",
		);
		expect(
			command(repo, ["git", "worktree", "list"])
				.split("\n")
				.filter((l) => l.includes(crashed.id)),
		).toHaveLength(1);

		// Moving the branch must not change what the run resumes from.
		command(repo, ["git", "checkout", "-q", "wip"]);
		writeFileSync(join(repo, "later.txt"), "later\n");
		command(repo, ["git", "add", "later.txt"]);
		command(repo, ["git", "commit", "-qm", "later"]);
		command(repo, ["git", "checkout", "-q", "main"]);

		const second = makeRunner(repo, agents, new FakeGates());
		const done = await second.runner.resume(crashed.id);
		expect(done.stage).toBe("DONE");
		expect(done.baseSha).toBe(wip);
		expect(done.worktreePath).toBe(crashed.worktreePath);
		expect(agents.createCount.get(`${crashed.id}:task-1:implementer:1`)).toBe(
			1,
		);
		expect(second.store.listRuns()).toHaveLength(1);
	});

	it("measures the diff against the base: inherited files do not force a build, new commits do", async () => {
		const repo = makeRepo();
		addWipCommit(repo);
		const docsOnly = { ...task(false), files: ["docs/README.md"] };

		const idle = new FakeGates();
		const quiet = makeRunner(repo, new FakeAgents(false), idle);
		const noEdit = await quiet.runner.start("inherit only", {
			tasks: [docsOnly],
			baseCommit: "wip",
		});
		expect(noEdit.stage).toBe("DONE");
		expect(idle.cwd).toEqual([]);

		const gates = new FakeGates();
		const busy = makeRunner(repo, new FakeAgents(true, false, true), gates);
		const edited = await busy.runner.start("new source", {
			tasks: [docsOnly],
			baseCommit: "wip",
		});
		expect(edited.stage).toBe("DONE");
		expect(gates.cwd).toEqual([edited.worktreePath!, edited.worktreePath!]);
		const handoff = readFileSync(
			join(
				repo,
				".harness",
				"runs",
				edited.id,
				"task-1",
				"handoff-1-implementer.md",
			),
			"utf8",
		);
		expect(handoff).toContain(`diff --stat ${edited.baseSha}`);
	});

	it("rejects an unresolvable ref without creating a run or worktree", async () => {
		const repo = makeRepo();
		const { runner, store } = makeRunner(
			repo,
			new FakeAgents(false),
			new FakeGates(),
		);
		for (const bad of ["no-such-ref", "--upload-pack=x", "  "]) {
			await expect(
				runner.start("bad", { tasks: [task(false)], baseCommit: bad }),
			).rejects.toThrow();
		}
		expect(store.listRuns()).toHaveLength(0);
		expect(existsSync(join(repo, ".harness", "worktrees"))).toBe(false);
	});

	it("rejects a commit that does not descend from origin/main", async () => {
		const repo = makeRepo();
		const unrelated = command(repo, [
			"git",
			"commit-tree",
			"4b825dc642cb6eb9a060e54bf8d69288fbee4904",
			"-m",
			"orphan",
		]).trim();
		const { runner, store } = makeRunner(
			repo,
			new FakeAgents(false),
			new FakeGates(),
		);
		await expect(
			runner.start("orphan", { tasks: [task(false)], baseCommit: unrelated }),
		).rejects.toThrow("does not descend from origin/main");
		expect(store.listRuns()).toHaveLength(0);
	});

	it("blocks resume when the existing worktree no longer descends from the persisted base", async () => {
		const repo = makeRepo();
		const wip = addWipCommit(repo);
		const agents = new FakeAgents(false, true);
		const first = makeRunner(repo, agents, new FakeGates());
		await expect(
			first.runner.start("tamper", { tasks: [task(false)], baseCommit: wip }),
		).rejects.toThrow("simulated crash");
		const crashed = first.store.listRuns()[0]!;
		command(crashed.worktreePath!, [
			"git",
			"reset",
			"-q",
			"--hard",
			"origin/main",
		]);
		const blocked = await first.runner.resume(crashed.id);
		expect(blocked.stage).toBe("BLOCKED");
		expect(blocked.parkedReason).toContain("persisted base");
	});
});
