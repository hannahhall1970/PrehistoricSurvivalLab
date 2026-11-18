import { afterEach, describe, expect, it } from "bun:test";
import {
	appendFileSync,
	mkdirSync,
	mkdtempSync,
	rmSync,
	writeFileSync,
} from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { TtyApprovalSource } from "../src/approval.ts";
import { Runner } from "../src/graph.ts";
import { Store } from "../src/store.ts";
import type {
	AgentPort,
	AgentRequest,
	AgentResult,
	FailureClass,
	GateResult,
	HarnessConfig,
	Role,
	TaskSpec,
	TaskState,
} from "../src/types.ts";
import type { ContextBuilder } from "../src/context.ts";
import type { Gates } from "../src/gates.ts";

/** Scripted human who reads the one-time code off the prompt and types it back. */
const typedBack = {
	ask: (prompt: string) => prompt.match(/Type ([0-9A-F]{6}) /)?.[1],
};

const stores: Store[] = [];
const directories: string[] = [];

afterEach(() => {
	for (const store of stores.splice(0)) store.close();
	for (const directory of directories.splice(0))
		rmSync(directory, { recursive: true, force: true });
});

function dbPath(): string {
	const directory = mkdtempSync(join(tmpdir(), "psl-harness-runner-"));
	directories.push(directory);
	return join(directory, "state.db");
}

function config(maxAttempts = 2): HarnessConfig {
	return {
		daemonUrl: "ws://fake",
		roles: {
			planner: { provider: "fake", modeId: "full" },
			implementer: { provider: "fake", modeId: "full" },
			reviewer: { provider: "fake", modeId: "ask" },
		},
		limits: {
			defaultMaxAttempts: maxAttempts,
			defaultBudgetMs: 1000,
			buildTimeoutMs: 1000,
			testTimeoutMs: 1000,
			preflightTimeoutMs: 1000,
			maxTasksPerRun: 12,
		},
		commands: { preflight: ["true"], build: ["true"], test: ["true"] },
		useWorktrees: false,
	};
}

function task(needsBuild = false, maxAttempts = 2): TaskSpec {
	return {
		id: "task-1",
		title: "Task",
		description: "Do the task",
		dependsOn: [],
		acceptance: ["It is complete"],
		files: ["docs/**"],
		needsBuild,
		maxAttempts,
		budgetMs: 1000,
	};
}

function gate(
	gateName: GateResult["gate"],
	outcome: GateResult["outcome"] = "SUCCESS",
	cls?: FailureClass,
): GateResult {
	return {
		gate: gateName,
		outcome,
		exitCode: outcome === "SUCCESS" ? 0 : null,
		durationMs: 1,
		command: ["fake", gateName],
		logPath: `${gateName.toLowerCase()}.log`,
		...(cls
			? {
					failure: {
						cls,
						summary: `${gateName} ${cls}`,
						logPath: `${gateName.toLowerCase()}.log`,
					},
				}
			: {}),
	};
}

class FakeGates implements Gates {
	buildResults: (GateResult | Error)[] = [];
	testResults: (GateResult | Error)[] = [];
	readonly calls: GateResult["gate"][] = [];

	preflight(): Promise<GateResult> {
		this.calls.push("PREFLIGHT");
		return Promise.resolve(gate("PREFLIGHT"));
	}

	async build(): Promise<GateResult> {
		this.calls.push("BUILD");
		const result = this.buildResults.shift() ?? gate("BUILD");
		if (result instanceof Error) throw result;
		return result;
	}

	async test(): Promise<GateResult> {
		this.calls.push("TEST");
		const result = this.testResults.shift() ?? gate("TEST");
		if (result instanceof Error) throw result;
		return result;
	}
}

class FakeContext implements ContextBuilder {
	roles: Role[] = [];
	tasks: string[] = [];

	async forPlanner(): Promise<string> {
		return "planner";
	}

	async forTask(
		_run: unknown,
		currentTask: { id: string; lastFailure?: { summary: string } },
		role: Role,
	): Promise<string> {
		this.roles.push(role);
		this.tasks.push(JSON.stringify(currentTask));
		return JSON.stringify(currentTask);
	}

	writeHandoff(
		_run: unknown,
		currentTask: { id: string },
		data: { role: Role; summary: string },
	): string {
		return `.harness/runs/fake/${currentTask.id}/${data.role}.md`;
	}
}

type AgentBehavior = (
	request: AgentRequest,
	call: number,
	signal?: AbortSignal,
) => AgentResult | Promise<AgentResult>;

class FakeAgents implements AgentPort {
	private readonly created = new Map<string, string>();
	private readonly calls = new Map<string, number>();
	private readonly behavior: AgentBehavior;
	readonly createCount = new Map<string, number>();
	readonly stopCalls: string[] = [];
	readonly createdSignal = Promise.withResolvers<string>();

	constructor(
		behavior: AgentBehavior = (request) => this.defaultResult(request),
	) {
		this.behavior = behavior;
	}

	async run(
		request: AgentRequest,
		signal?: AbortSignal,
		hooks?: { onCreated?(agentId: string): void },
	): Promise<AgentResult> {
		let agentId = this.created.get(request.idempotencyKey);
		if (!agentId) {
			agentId = `agent-${this.created.size + 1}`;
			this.created.set(request.idempotencyKey, agentId);
			this.createCount.set(
				request.idempotencyKey,
				(this.createCount.get(request.idempotencyKey) ?? 0) + 1,
			);
			hooks?.onCreated?.(agentId);
			this.createdSignal.resolve(agentId);
		}
		const call = (this.calls.get(request.idempotencyKey) ?? 0) + 1;
		this.calls.set(request.idempotencyKey, call);
		return this.behavior(request, call, signal);
	}

	async stop(agentId: string): Promise<void> {
		this.stopCalls.push(agentId);
	}

	countForKey(key: string): number {
		return this.createCount.get(key) ?? 0;
	}

	private defaultResult(request: AgentRequest): AgentResult {
		if (request.role === "reviewer") {
			return {
				status: "idle",
				agentId: "unused",
				structured: { verdict: "approve", findings: [] },
			};
		}
		if (request.role === "planner") {
			return {
				status: "idle",
				agentId: "unused",
				structured: { tasks: [task()] },
			};
		}
		return { status: "idle", agentId: "unused", text: "all done" };
	}
}
class StopOnceAgents extends FakeAgents {
	failNextStop = true;

	override async stop(agentId: string): Promise<void> {
		this.stopCalls.push(agentId);
		if (this.failNextStop) {
			this.failNextStop = false;
			throw new Error("stop RPC unavailable");
		}
	}
}

function runner(
	path: string,
	agents: AgentPort,
	gates = new FakeGates(),
	owner = "test-owner",
	repoRoot = process.cwd(),
) {
	const store = new Store(path);
	stores.push(store);
	return {
		store,
		runner: new Runner({
			store,
			agents,
			gates,
			context: new FakeContext(),
			config: config(),
			repoRoot,
			owner,
		}),
		gates,
	};
}
class CrashBeforeTaskPersistStore extends Store {
	private failWorkerPersist = true;

	override saveTask(runId: string, currentTask: TaskState): void {
		if (this.failWorkerPersist && currentTask.workerAgentId) {
			this.failWorkerPersist = false;
			throw new Error("process died before task persistence");
		}
		super.saveTask(runId, currentTask);
	}
}

function gitRepoPath(): string {
	const root = mkdtempSync(join(tmpdir(), "psl-harness-policy-"));
	directories.push(root);
	const result = Bun.spawnSync(["git", "init", "-q", root], {
		stdout: "pipe",
		stderr: "pipe",
	});
	if (result.exitCode !== 0) throw new Error("git init failed");
	writeFileSync(join(root, "tracked.txt"), "base\n");
	for (const args of [
		["git", "config", "user.email", "harness@example.test"],
		["git", "config", "user.name", "Harness Test"],
		["git", "add", "tracked.txt"],
		["git", "commit", "-qm", "base"],
	]) {
		const commit = Bun.spawnSync(args, {
			cwd: root,
			stdout: "pipe",
			stderr: "pipe",
		});
		if (commit.exitCode !== 0)
			throw new Error(`git command failed: ${args.join(" ")}`);
	}
	return root;
}

function fingerprintFixture(): {
	repo: string;
	run: { id: string; worktreePath: string; worktreeBranch: string };
} {
	const repo = gitRepoPath();
	mkdirSync(join(repo, "Source"), { recursive: true });
	writeFileSync(join(repo, "Source", "a.cpp"), "int value = 1;\n");
	for (const args of [
		["git", "add", "Source/a.cpp"],
		["git", "commit", "-qm", "source"],
	]) {
		const result = Bun.spawnSync(args, {
			cwd: repo,
			stdout: "pipe",
			stderr: "pipe",
		});
		if (result.exitCode !== 0)
			throw new Error(`git command failed: ${args.join(" ")}`);
	}
	const id = crypto.randomUUID();
	const worktreePath = join(repo, ".harness", "worktrees", id);
	const worktreeBranch = `harness/${id}`;
	mkdirSync(join(repo, ".harness", "worktrees"), { recursive: true });
	const worktree = Bun.spawnSync(
		[
			"git",
			"worktree",
			"add",
			"-q",
			"-b",
			worktreeBranch,
			worktreePath,
			"HEAD",
		],
		{ cwd: repo, stdout: "pipe", stderr: "pipe" },
	);
	if (worktree.exitCode !== 0) throw new Error("git worktree add failed");
	return { repo, run: { id, worktreePath, worktreeBranch } };
}

describe("Runner.treeFingerprint", () => {
	it("fails closed when a tracked build input is skip-worktree", () => {
		const fixture = fingerprintFixture();
		const { runner: instance } = runner(
			dbPath(),
			new FakeAgents(),
			new FakeGates(),
			"fingerprint",
			fixture.repo,
		);
		const result = Bun.spawnSync(
			["git", "update-index", "--skip-worktree", "Source/a.cpp"],
			{ cwd: fixture.run.worktreePath, stdout: "pipe", stderr: "pipe" },
		);
		expect(result.exitCode).toBe(0);
		expect(callTreeFingerprint(instance, fixture.run)).toBeNull();
	});

	it("changes when a tracked Source file is edited", () => {
		const fixture = fingerprintFixture();
		const { runner: instance } = runner(
			dbPath(),
			new FakeAgents(),
			new FakeGates(),
			"fingerprint",
			fixture.repo,
		);
		const before = callTreeFingerprint(instance, fixture.run);
		expect(before).not.toBeNull();
		writeFileSync(
			join(fixture.run.worktreePath, "Source", "a.cpp"),
			"int value = 2;\n",
		);
		expect(callTreeFingerprint(instance, fixture.run)).not.toBe(before);
	});

	it("changes when a gitignored Plugins source file appears or is edited", () => {
		const fixture = fingerprintFixture();
		const { runner: instance } = runner(
			dbPath(),
			new FakeAgents(),
			new FakeGates(),
			"fingerprint",
			fixture.repo,
		);
		const wt = fixture.run.worktreePath;
		// ignore rule lives in the shared info/exclude so the tracked tree stays untouched
		const exclude = Bun.spawnSync(
			["git", "rev-parse", "--git-path", "info/exclude"],
			{
				cwd: wt,
				stdout: "pipe",
			},
		);
		const excludePath = join(
			wt,
			new TextDecoder().decode(exclude.stdout).trim(),
		);
		mkdirSync(join(excludePath, ".."), { recursive: true });
		appendFileSync(excludePath, "\nPlugins/\n");
		const before = callTreeFingerprint(instance, fixture.run);
		expect(before).not.toBeNull();
		mkdirSync(join(wt, "Plugins", "X", "Source"), { recursive: true });
		const file = join(wt, "Plugins", "X", "Source", "x.cpp");
		writeFileSync(file, "int a;\n");
		const added = callTreeFingerprint(instance, fixture.run);
		expect(added).not.toBe(before);
		writeFileSync(file, "int b;\n");
		expect(callTreeFingerprint(instance, fixture.run)).not.toBe(added);
	});
});
type FingerprintRun = {
	id: string;
	worktreePath: string;
	worktreeBranch: string;
};

function callTreeFingerprint(
	instance: Runner,
	run: FingerprintRun,
): string | null {
	const privateRunner = instance as unknown as {
		treeFingerprint(run: FingerprintRun): string | null;
	};
	return privateRunner.treeFingerprint(run);
}

describe("PSL.Runner", () => {
	it("invalidates gate evidence when review mutates the task tree", async () => {
		const repo = gitRepoPath();
		const agents = new FakeAgents(async (request) => {
			if (request.role === "reviewer") {
				writeFileSync(join(request.cwd, "review-change.txt"), "changed\n");
				return {
					status: "idle",
					agentId: "reviewer",
					structured: { verdict: "approve", findings: [] },
				};
			}
			return { status: "idle", agentId: "worker" };
		});
		const { runner: instance, store } = runner(
			dbPath(),
			agents,
			new FakeGates(),
			"test-owner",
			repo,
		);
		const run = await instance.start("review mutation", {
			tasks: [task(true, 1)],
		});
		expect(run.stage).toBe("FAILED");
		expect(store.getTasks(run.id)[0]?.lastFailure?.summary).toContain(
			"verification evidence invalidated",
		);
	});

	it("invalidates gate evidence when review stages a post-test change", async () => {
		const repo = gitRepoPath();
		const agents = new FakeAgents(async (request) => {
			if (request.role === "implementer") {
				writeFileSync(join(request.cwd, "tracked.txt"), "worker change\n");
			}
			if (request.role === "reviewer") {
				const staged = Bun.spawnSync(["git", "add", "tracked.txt"], {
					cwd: request.cwd,
					stdout: "pipe",
					stderr: "pipe",
				});
				if (staged.exitCode !== 0) throw new Error("git add failed");
				return {
					status: "idle",
					agentId: "reviewer",
					structured: { verdict: "approve", findings: [] },
				};
			}
			return { status: "idle", agentId: "worker" };
		});
		const { runner: instance, store } = runner(
			dbPath(),
			agents,
			new FakeGates(),
			"test-owner",
			repo,
		);
		const run = await instance.start("staged mutation", {
			tasks: [task(true, 1)],
		});
		expect(run.stage).toBe("FAILED");
		expect(store.getTasks(run.id)[0]?.lastFailure?.summary).toContain(
			"verification evidence invalidated",
		);
	});

	it("fails closed when git cannot produce a tree fingerprint", async () => {
		const repo = mkdtempSync(join(tmpdir(), "psl-harness-no-git-"));
		directories.push(repo);
		const { runner: instance, store } = runner(
			dbPath(),
			new FakeAgents(),
			new FakeGates(),
			"test-owner",
			repo,
		);
		const run = await instance.start("missing git", {
			tasks: [task(true, 1)],
		});
		expect(run.stage).toBe("FAILED");
		expect(store.getTasks(run.id)[0]?.lastFailure?.summary).toContain(
			"verification evidence invalidated",
		);
	});

	it("forces build and test when an untracked C++ file appears", async () => {
		const repo = gitRepoPath();
		const agents = new FakeAgents(async (request) => {
			if (request.role === "implementer") {
				await Bun.write(join(request.cwd, "new.cpp"), "int main() {}\n");
				return { status: "idle", agentId: "worker" };
			}
			if (request.role === "reviewer") {
				return {
					status: "idle",
					agentId: "reviewer",
					structured: { verdict: "approve", findings: [] },
				};
			}
			return { status: "idle", agentId: "planner" };
		});
		const gates = new FakeGates();
		const { runner: instance, store } = runner(
			dbPath(),
			agents,
			gates,
			"test-owner",
			repo,
		);
		const run = await instance.start("untracked source", { tasks: [task()] });
		expect(run.stage).toBe("DONE");
		expect(gates.calls).toEqual(["PREFLIGHT", "BUILD", "TEST"]);
		expect(store.getTasks(run.id)[0]?.needsBuild).toBe(true);
		const policyEvent = store
			.events(run.id)
			.find((event) => event.kind === "agent" && event.stage === "EXECUTE");
		expect(policyEvent?.data.buildPolicy).toMatchObject({
			required: true,
			changedFiles: ["new.cpp"],
		});
	});

	it("does not pass a source-changing task when the forced test gate fails", async () => {
		const repo = gitRepoPath();
		const agents = new FakeAgents(async (request) => {
			if (request.role === "implementer") {
				await Bun.write(join(request.cwd, "new.cpp"), "int main() {}\n");
				return { status: "idle", agentId: "worker" };
			}
			return {
				status: "idle",
				agentId: "reviewer",
				structured: { verdict: "approve", findings: [] },
			};
		});
		const gates = new FakeGates();
		gates.testResults = [gate("TEST", "FAILURE", "TEST_FAILURE")];
		const { runner: instance, store } = runner(
			dbPath(),
			agents,
			gates,
			"test-owner",
			repo,
		);
		const run = await instance.start("failed forced test", {
			tasks: [task(false, 1)],
		});
		expect(run.stage).toBe("FAILED");
		expect(store.getTasks(run.id)[0]?.status).toBe("FAILED");
	});

	it("runs a provided task through execute, review, checkpoint, and done", async () => {
		const agents = new FakeAgents();
		const { runner: instance, store } = runner(dbPath(), agents);
		const run = await instance.start("ship it", { tasks: [task()] });
		expect(run.stage).toBe("DONE");
		expect(store.getTasks(run.id)[0]?.status).toBe("PASSED");
		expect(
			store.events(run.id).some((event) => event.stage === "CHECKPOINT"),
		).toBe(true);
	});
	it("rejects an empty task contract", async () => {
		const { runner: instance } = runner(dbPath(), new FakeAgents());
		const run = await instance.start("empty", { tasks: [] });
		expect(run.stage).toBe("FAILED");
	});

	it("parks a timeout when stopping the agent fails and retries the stop on resume", async () => {
		let workerCalls = 0;
		const agents = new StopOnceAgents((request) => {
			if (request.role === "implementer") {
				workerCalls += 1;
				return workerCalls === 1
					? { status: "timeout", agentId: "timed-out" }
					: { status: "idle", agentId: "worker" };
			}
			return {
				status: "idle",
				agentId: "reviewer",
				structured: { verdict: "approve", findings: [] },
			};
		});
		const path = dbPath();
		const first = runner(path, agents);
		const parked = await first.runner.start("stop failure", {
			tasks: [task(false, 2)],
		});
		expect(parked.stage).toBe("BLOCKED");
		expect(parked.parkedReason).toContain("timed-out");
		expect(parked.parkedReason).toContain("stop RPC unavailable");
		const second = runner(path, agents, new FakeGates(), "resume-owner");
		expect((await second.runner.resume(parked.id)).stage).toBe("DONE");
		expect(agents.stopCalls).toEqual(["timed-out", "timed-out"]);
	});

	it("stops a parked run immediately and keeps it stopped on resume", async () => {
		const agents = new FakeAgents((request) =>
			request.role === "implementer"
				? {
						status: "permission",
						agentId: "needs-permission",
						permission: { requestId: "p1", description: "write" },
					}
				: {
						status: "idle",
						agentId: "reviewer",
						structured: { verdict: "approve", findings: [] },
					},
		);
		const path = dbPath();
		const first = runner(path, agents);
		const parked = await first.runner.start("parked stop", { tasks: [task()] });
		first.runner.requestStop(parked.id);
		expect(first.store.getRun(parked.id)?.stage).toBe("STOPPED");
		expect((await first.runner.resume(parked.id)).stage).toBe("STOPPED");
	});

	it("does not pass a worker that claims success when the build fails", async () => {
		const agents = new FakeAgents();
		const gates = new FakeGates();
		gates.buildResults = [gate("BUILD", "FAILURE", "COMPILE_ERROR")];
		const { runner: instance, store } = runner(dbPath(), agents, gates);
		const run = await instance.start("build", { tasks: [task(true, 1)] });
		expect(run.stage).toBe("FAILED");
		expect(store.getTasks(run.id)[0]?.status).toBe("FAILED");
		expect(store.getTasks(run.id)[0]?.lastFailure?.cls).toBe("COMPILE_ERROR");
	});

	it("retries a test failure and includes a rejected review in the next context", async () => {
		let reviewerCalls = 0;
		const agents = new FakeAgents((request) => {
			if (request.role === "reviewer") reviewerCalls += 1;
			if (request.role === "reviewer" && reviewerCalls === 1) {
				return {
					status: "idle",
					agentId: "reviewer-1",
					structured: {
						verdict: "reject",
						findings: [{ id: "F1", severity: "blocker", detail: "bad" }],
					},
				};
			}
			if (request.role === "reviewer")
				return {
					status: "idle",
					agentId: "reviewer-2",
					structured: { verdict: "approve", findings: [] },
				};
			return { status: "idle", agentId: "worker", text: "all done" };
		});
		const gates = new FakeGates();
		gates.testResults = [gate("TEST", "FAILURE", "TEST_FAILURE"), gate("TEST")];
		const { runner: instance, store } = runner(dbPath(), agents, gates);
		const run = await instance.start("retry", { tasks: [task(true, 3)] });
		expect(run.stage).toBe("DONE");
		expect(store.getTasks(run.id)[0]?.attempt).toBe(3);
		expect(
			store
				.events(run.id)
				.some((event) => JSON.stringify(event.data).includes("bad")),
		).toBe(true);
	});

	it("distinguishes timeout, stops the timed out agent, and counts the attempt", async () => {
		let implementerCalls = 0;
		const agents = new FakeAgents((request) => {
			if (request.role === "implementer") implementerCalls += 1;
			if (request.role === "implementer" && implementerCalls === 1) {
				return { status: "timeout", agentId: "timed-out" };
			}
			if (request.role === "reviewer") {
				return {
					status: "idle",
					agentId: "reviewer",
					structured: { verdict: "approve", findings: [] },
				};
			}
			return { status: "idle", agentId: "worker" };
		});
		const { runner: instance, store } = runner(dbPath(), agents);
		const run = await instance.start("timeout", { tasks: [task(false, 2)] });
		expect(run.stage).toBe("DONE");
		expect(agents.stopCalls).toContain("timed-out");
		expect(store.getTasks(run.id)[0]?.attempt).toBe(2);
	});

	it("accepts a nested ReviewVerdict envelope without spending an implementer attempt", async () => {
		let implementerCalls = 0;
		const agents = new FakeAgents((request) => {
			if (request.role === "implementer") implementerCalls += 1;
			return request.role === "reviewer"
				? {
						status: "idle",
						agentId: "reviewer",
						structured: { ReviewVerdict: { verdict: "approve", findings: [] } },
					}
				: { status: "idle", agentId: "worker" };
		});
		const { runner: instance } = runner(dbPath(), agents);
		const run = await instance.start("nested", { tasks: [task(false, 2)] });
		expect(run.stage).toBe("DONE");
		expect(implementerCalls).toBe(1);
	});

	it("retries only the reviewer after a protocol error and keeps the implementer attempt", async () => {
		let implementerCalls = 0;
		let reviewerCalls = 0;
		const agents = new FakeAgents((request) => {
			if (request.role === "implementer") implementerCalls += 1;
			if (request.role !== "reviewer")
				return { status: "idle", agentId: "worker" };
			reviewerCalls += 1;
			return reviewerCalls === 1
				? { status: "idle", agentId: "reviewer-1", text: "I approve." }
				: {
						status: "idle",
						agentId: "reviewer-2",
						structured: { verdict: "approve", findings: [] },
					};
		});
		const { runner: instance, store } = runner(dbPath(), agents);
		const run = await instance.start("protocol", { tasks: [task(false, 2)] });
		expect(run.stage).toBe("DONE");
		expect(implementerCalls).toBe(1);
		expect(reviewerCalls).toBe(2);
		expect(store.getTasks(run.id)[0]?.attempt).toBe(1);
		expect(
			store.events(run.id).filter((e) => e.kind === "review_protocol_error"),
		).toHaveLength(1);
	});

	it("parks the run as BLOCKED after bounded reviewer protocol errors, never failing the task", async () => {
		let implementerCalls = 0;
		let reviewerCalls = 0;
		const agents = new FakeAgents((request) => {
			if (request.role === "implementer") implementerCalls += 1;
			if (request.role === "reviewer") {
				reviewerCalls += 1;
				return {
					status: "idle",
					agentId: `reviewer-${reviewerCalls}`,
					text: "{broken",
				};
			}
			return { status: "idle", agentId: "worker" };
		});
		const { runner: instance, store } = runner(dbPath(), agents);
		const run = await instance.start("blocked", { tasks: [task(false, 3)] });
		expect(run.stage).toBe("BLOCKED");
		expect(run.parkedReason).toContain("resumeStage=INDEPENDENT_REVIEW");
		expect(reviewerCalls).toBe(3);
		expect(implementerCalls).toBe(1);
		const stored = store.getTasks(run.id)[0];
		expect(stored?.attempt).toBe(1);
		expect(stored?.lastFailure?.cls).toBe("REVIEW_PROTOCOL_ERROR");
	});

	it("does not approve a conflicting verdict and still passes through reject", async () => {
		let reviewerCalls = 0;
		const agents = new FakeAgents((request) => {
			if (request.role !== "reviewer")
				return { status: "idle", agentId: "worker" };
			reviewerCalls += 1;
			return {
				status: "idle",
				agentId: `reviewer-${reviewerCalls}`,
				structured:
					reviewerCalls === 1
						? {
								verdict: "approve",
								findings: [],
								ReviewVerdict: { verdict: "approve", findings: [] },
							}
						: {
								ReviewVerdict: {
									verdict: "reject",
									findings: [{ id: "F1", severity: "blocker", detail: "bad" }],
								},
							},
			};
		});
		const { runner: instance, store } = runner(dbPath(), agents);
		const run = await instance.start("conflict", { tasks: [task(false, 1)] });
		expect(run.stage).toBe("FAILED");
		expect(store.getTasks(run.id)[0]?.lastFailure?.cls).toBe("REVIEW_REJECTED");
		expect(reviewerCalls).toBe(2);
	});

	it("parks permission and resumes with the same worker key", async () => {
		const agents = new FakeAgents((request, call) => {
			if (request.role === "implementer" && call === 1) {
				return {
					status: "permission",
					agentId: "needs-permission",
					permission: { requestId: "p1", description: "write" },
				};
			}
			return request.role === "reviewer"
				? {
						status: "idle",
						agentId: "reviewer",
						structured: { verdict: "approve", findings: [] },
					}
				: { status: "idle", agentId: "worker" };
		});
		const path = dbPath();
		const first = runner(path, agents);
		const parked = await first.runner.start("permission", { tasks: [task()] });
		expect(parked.stage).toBe("HUMAN_APPROVAL");
		const key = `${parked.id}:task-1:implementer:1`;
		const second = runner(path, agents, new FakeGates(), "resume-owner");
		// Without a trusted human channel, --approve alone stays parked (fail closed).
		const refused = await second.runner.resume(parked.id, { approve: true });
		expect(refused.stage).toBe("HUMAN_APPROVAL");
		const done = await second.runner.resume(parked.id, {
			approve: true,
			approvals: new TtyApprovalSource(typedBack),
		});
		expect(done.stage).toBe("DONE");
		expect(agents.countForKey(key)).toBe(1);
	});

	it("deduplicates a crash after agent creation before task persistence", async () => {
		const agents = new FakeAgents();
		const path = dbPath();
		const firstStore = new CrashBeforeTaskPersistStore(path);
		stores.push(firstStore);
		const first = new Runner({
			store: firstStore,
			agents,
			gates: new FakeGates(),
			context: new FakeContext(),
			config: config(),
			repoRoot: process.cwd(),
			owner: "crashed-owner",
		});
		await expect(
			first.start("crash before persistence", { tasks: [task()] }),
		).rejects.toThrow("process died before task persistence");
		const crashedRun = firstStore.listRuns()[0]!;
		expect(
			firstStore.getTasks(crashedRun.id)[0]?.workerAgentId,
		).toBeUndefined();
		const second = runner(path, agents, new FakeGates(), "resumed-owner");
		const done = await second.runner.resume(crashedRun.id);
		expect(done.stage).toBe("DONE");
		expect(agents.countForKey(`${crashedRun.id}:task-1:implementer:1`)).toBe(1);
	});

	it("fails the run after build retry exhaustion at maxAttempts", async () => {
		const maxAttempts = 3;
		let implementerCalls = 0;
		const agents = new FakeAgents((request) => {
			if (request.role === "implementer") implementerCalls += 1;
			return request.role === "reviewer"
				? {
						status: "idle",
						agentId: "reviewer",
						structured: { verdict: "approve", findings: [] },
					}
				: { status: "idle", agentId: "worker" };
		});
		const gates = new FakeGates();
		gates.buildResults = Array.from({ length: maxAttempts }, () =>
			gate("BUILD", "FAILURE", "COMPILE_ERROR"),
		);
		const { runner: instance, store } = runner(dbPath(), agents, gates);
		const run = await instance.start("always fails", {
			tasks: [task(true, maxAttempts)],
		});
		expect(run.stage).toBe("FAILED");
		expect(store.getTasks(run.id)[0]?.status).toBe("FAILED");
		expect(implementerCalls).toBe(maxAttempts);
	});
	it("resumes the same idempotency key after a worker crash", async () => {
		let crashed = false;
		const agents = new FakeAgents((request) => {
			if (request.role === "implementer" && !crashed) {
				crashed = true;
				throw new Error("simulated crash");
			}
			return request.role === "reviewer"
				? {
						status: "idle",
						agentId: "reviewer",
						structured: { verdict: "approve", findings: [] },
					}
				: { status: "idle", agentId: "worker" };
		});
		const path = dbPath();
		const first = runner(path, agents);
		await expect(
			first.runner.start("crash", { tasks: [task()] }),
		).rejects.toThrow("simulated crash");
		const crashedRun = first.store.listRuns()[0];
		expect(crashedRun?.stage).toBe("EXECUTE");
		expect(first.store.getTasks(crashedRun!.id)[0]?.workerAgentId).toBe(
			"agent-1",
		);
		const second = runner(path, agents, new FakeGates(), "new-owner");
		const done = await second.runner.resume(crashedRun!.id);
		expect(done.stage).toBe("DONE");
		expect(agents.countForKey(`${crashedRun!.id}:task-1:implementer:1`)).toBe(
			1,
		);
	});

	it("resumes after a verification crash without creating a second worker", async () => {
		const agents = new FakeAgents();
		const gates = new FakeGates();
		gates.buildResults = [new Error("gate crash")];
		const path = dbPath();
		const first = runner(path, agents, gates);
		await expect(
			first.runner.start("gate crash", { tasks: [task(true)] }),
		).rejects.toThrow("gate crash");
		const crashedRun = first.store.listRuns()[0]!;
		expect(crashedRun.stage).toBe("BUILD");
		const second = runner(path, agents, new FakeGates(), "new-owner");
		const done = await second.runner.resume(crashedRun.id);
		expect(done.stage).toBe("DONE");
		expect(agents.countForKey(`${crashedRun.id}:task-1:implementer:1`)).toBe(1);
	});

	it("stops an in-flight worker and records STOPPED", async () => {
		const gateAgents = new FakeAgents(async (request, _call, signal) => {
			if (request.role !== "implementer")
				return {
					status: "idle",
					agentId: "reviewer",
					structured: { verdict: "approve", findings: [] },
				};
			await new Promise<void>((resolve) =>
				signal?.addEventListener("abort", () => resolve(), { once: true }),
			);
			return { status: "error", agentId: "worker", error: "cancelled" };
		});
		const path = dbPath();
		const first = runner(path, gateAgents);
		const started = first.runner.start("cancel", { tasks: [task()] });
		await gateAgents.createdSignal.promise;
		const run = first.store.listRuns()[0]!;
		first.runner.requestStop(run.id);
		const stopped = await started;
		expect(stopped.stage).toBe("STOPPED");
	});

	it("rejects a second active repo writer and reclaims an expired lease", async () => {
		const agents = new FakeAgents(async (request, _call, signal) => {
			if (request.role === "implementer") {
				await new Promise<void>((resolve) =>
					signal?.addEventListener("abort", () => resolve(), { once: true }),
				);
			}
			return request.role === "reviewer"
				? {
						status: "idle",
						agentId: "reviewer",
						structured: { verdict: "approve", findings: [] },
					}
				: { status: "idle", agentId: "worker" };
		});
		const path = dbPath();
		const first = runner(path, agents, new FakeGates(), "owner-a");
		const firstRunPromise = first.runner.start("one", { tasks: [task()] });
		await agents.createdSignal.promise;
		const second = runner(path, new FakeAgents(), new FakeGates(), "owner-b");
		const blocked = await second.runner.start("two", { tasks: [task()] });
		expect(blocked.stage).toBe("BLOCKED");
		const leaseStore = new Store(path);
		stores.push(leaseStore);
		const leaseRun = leaseStore.createRun("lease", "lease-test");
		expect(leaseStore.acquireRunLease(leaseRun.id, "stale", 1)).toBe(true);
		await new Promise((resolve) => setTimeout(resolve, 5));
		expect(leaseStore.acquireRunLease(leaseRun.id, "reclaimer", 1000)).toBe(
			true,
		);
		first.runner.requestStop(
			(
				first.store.listRuns().find((run) => run.objective === "one") as {
					id: string;
				}
			).id,
		);
		await firstRunPromise;
	});

	it("rejects concurrent resume and makes repeated terminal resumes harmless", async () => {
		const agents = new FakeAgents(async (request, _call, signal) => {
			if (request.role === "implementer") {
				await new Promise<void>((resolve) =>
					signal?.addEventListener("abort", () => resolve(), { once: true }),
				);
			}
			return request.role === "reviewer"
				? {
						status: "idle",
						agentId: "reviewer",
						structured: { verdict: "approve", findings: [] },
					}
				: { status: "idle", agentId: "worker" };
		});
		const path = dbPath();
		const first = runner(path, agents, new FakeGates(), "owner-a");
		const running = first.runner.start("resume", { tasks: [task()] });
		await agents.createdSignal.promise;
		const run = first.store.listRuns()[0]!;
		const second = runner(path, new FakeAgents(), new FakeGates(), "owner-b");
		await expect(second.runner.resume(run.id)).rejects.toThrow("leased");
		first.runner.requestStop(run.id);
		const stopped = await running;
		expect(stopped.stage).toBe("STOPPED");
		expect((await second.runner.resume(run.id)).stage).toBe("STOPPED");
		expect((await second.runner.resume(run.id)).stage).toBe("STOPPED");
	});

	it("reads state written by a child Bun process after the parent store closes", async () => {
		const path = dbPath();
		const storeUrl = new URL("../src/store.ts", import.meta.url).href;
		const script = `const { Store } = await import(${JSON.stringify(storeUrl)}); const store = new Store(${JSON.stringify(path)}); const run = store.createRun("child", "child-run"); store.saveRun({ ...run, stage: "PREFLIGHT" }); store.close();`;
		const child = Bun.spawn(["bun", "-e", script], {
			cwd: process.cwd(),
			stdout: "pipe",
			stderr: "pipe",
		});
		expect(await child.exited).toBe(0);
		const store = new Store(path);
		stores.push(store);
		expect(store.getRun("child-run")?.stage).toBe("PREFLIGHT");
	});
});
