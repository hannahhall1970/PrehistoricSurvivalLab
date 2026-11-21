import { afterEach, describe, expect, it } from "bun:test";
import {
	chmodSync,
	existsSync,
	mkdtempSync,
	rmSync,
	writeFileSync,
} from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import {
	type ApprovalIO,
	confirmTransaction,
	type ReleaseTransaction,
	requireApproval,
} from "../src/approval.ts";
import { RepoContextBuilder } from "../src/context.ts";
import {
	classifyCommand,
	executeGitOp,
	type GitOp,
	type GitOpDeps,
	type GitOpEvent,
} from "../src/gitops.ts";
import { Runner } from "../src/graph.ts";
import { Store } from "../src/store.ts";
import type {
	AgentPort,
	AgentRequest,
	AgentResult,
	GateResult,
	HarnessConfig,
	RunState,
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

/** Temp repo on `main` with a local bare `origin`; no network, never the real repo. */
function makeRepo(): { repo: string; origin: string } {
	const root = mkdtempSync(join(tmpdir(), "psl-release-"));
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
	return { repo, origin };
}

const config = (): HarnessConfig => ({
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
});

const task: TaskSpec = {
	id: "task-1",
	title: "Edit isolated file",
	description: "Make the requested edit in the worktree",
	dependsOn: [],
	acceptance: ["The worktree contains the edit"],
	files: ["tracked.txt"],
	needsBuild: true,
	maxAttempts: 2,
	budgetMs: 1000,
};

const gate = (name: GateResult["gate"]): GateResult => ({
	gate: name,
	outcome: "SUCCESS",
	exitCode: 0,
	durationMs: 0,
	command: ["true"],
	logPath: `${name}.log`,
});

class FakeGates implements Gates {
	preflight = async () => gate("PREFLIGHT");
	build = async () => gate("BUILD");
	test = async () => gate("TEST");
}

/** Implementer edits tracked.txt (or asks for `permission`); reviewer approves. */
class FakeAgents implements AgentPort {
	constructor(
		private readonly permission?: string,
		private readonly extra?: string,
	) {}
	async run(
		request: AgentRequest,
		_signal?: AbortSignal,
		hooks?: { onCreated?(agentId: string): void },
	): Promise<AgentResult> {
		hooks?.onCreated?.(`agent-${request.role}`);
		if (request.role === "implementer") {
			if (this.permission)
				return {
					status: "permission",
					agentId: "implementer",
					permission: { requestId: "p1", description: this.permission },
				};
			writeFileSync(join(request.cwd, "tracked.txt"), "edited\n");
			if (this.extra) writeFileSync(join(request.cwd, this.extra), "x\n");
			return { status: "idle", agentId: "implementer" };
		}
		return {
			status: "idle",
			agentId: "reviewer",
			structured: { verdict: "approve", findings: [] },
		};
	}
	async stop(): Promise<void> {}
}

function makeRunner(repo: string, agents: AgentPort = new FakeAgents()) {
	const store = new Store(join(repo, ".harness", "state.db"));
	stores.push(store);
	const runner = new Runner({
		store,
		agents,
		gates: new FakeGates(),
		context: new RepoContextBuilder({
			repoRoot: repo,
			artifactDir: join(repo, ".harness"),
		}),
		config: config(),
		repoRoot: repo,
		owner: `test-${Math.random()}`,
	});
	return { store, runner };
}

/** Scripted human who reads the one-time code off the prompt and types it back, counting asks. */
function typist() {
	const io = {
		asks: 0,
		ask(prompt: string): string | undefined {
			io.asks += 1;
			return prompt.match(/Type ([0-9A-F]{6}) /)?.[1];
		},
	};
	return io;
}

/** Human channel that must never be consulted. */
function forbidden() {
	const io = {
		asks: 0,
		ask(): string | undefined {
			io.asks += 1;
			throw new Error("a human form was requested for a safe operation");
		},
	};
	return io;
}

async function finishedRun() {
	const { repo, origin } = makeRepo();
	const { store, runner } = makeRunner(repo);
	const run = await runner.start("edit", { tasks: [task] });
	return { repo, origin, store, runner, run };
}

const sha = (cwd: string, ref: string) =>
	command(cwd, ["git", "rev-parse", ref]).trim();
const originMain = (origin: string) => sha(origin, "refs/heads/main");

function events(store: Store, run: RunState, kind: string) {
	return store.events(run.id).filter((event) => event.kind === kind);
}

function transactionFor(
	repo: string,
	run: RunState,
	override: Partial<ReleaseTransaction> = {},
): ReleaseTransaction {
	return {
		runId: run.id,
		runVersion: run.version,
		sourceBranch: `harness/${run.id}`,
		targetBranch: "main",
		sourceSha: sha(repo, `harness/${run.id}`),
		targetSha: sha(repo, "main"),
		remoteUrl: command(repo, ["git", "remote", "get-url", "origin"]).trim(),
		files: ["tracked.txt"],
		lfsObjects: [],
		operations: ["git.merge", "git.push"],
		...override,
	};
}

function depsFor(
	repo: string,
	store: Store,
	run: RunState,
	extra: Partial<GitOpDeps> = {},
): GitOpDeps {
	return {
		repoRoot: repo,
		runId: run.id,
		runVersion: run.version,
		log: (event: GitOpEvent) =>
			store.appendEvent({
				runId: run.id,
				stage: "DONE",
				kind: event.kind,
				data: event.data,
			}),
		...extra,
	};
}

describe("A. safe ops run without a form", () => {
	it("reaches DONE with the task commit on harness/<runId> and never asks a human", async () => {
		const human = forbidden();
		const { repo, store, run } = await finishedRun();
		expect(run.stage).toBe("DONE");
		expect(
			command(repo, [
				"git",
				"log",
				"-1",
				"--format=%s",
				`harness/${run.id}`,
			]).trim(),
		).toBe(`harness(${run.id}): task-1 Edit isolated file`);
		expect(
			command(repo, [
				"git",
				"rev-list",
				"--count",
				`main..harness/${run.id}`,
			]).trim(),
		).toBe("1");
		const auto = events(store, run, "auto_op");
		for (const opId of ["stage.exact", "commit.task"])
			expect(
				auto.some((e) => e.data.opId === opId && e.data.outcome === "ok"),
			).toBe(true);
		expect(human.asks).toBe(0);
		expect(events(store, run, "protected_op")).toHaveLength(0);
	});
});

describe("B. protected actions still stop", () => {
	for (const [name, answer] of [
		["no approval", undefined],
		["wrong nonce", "WRONG1"],
	] as const) {
		it(`release with ${name} leaves main and origin untouched and records a denied protected_op`, async () => {
			const { repo, origin, store, runner, run } = await finishedRun();
			const localBefore = sha(repo, "main");
			const originBefore = originMain(origin);
			const io: ApprovalIO = { ask: () => answer };
			const result = runner.release(run.id, { approvals: io });
			expect(result.ok).toBe(false);
			expect(sha(repo, "main")).toBe(localBefore);
			expect(originMain(origin)).toBe(originBefore);
			const denied = events(store, run, "protected_op").filter(
				(e) => e.data.outcome === "denied",
			);
			expect(denied).toHaveLength(1);
			expect(denied[0]?.data.argv).toBeNull();
		});
	}

	it("release without any approval source does nothing", async () => {
		const { repo, origin, runner, run } = await finishedRun();
		const before = originMain(origin);
		expect(runner.release(run.id).ok).toBe(false);
		expect(originMain(origin)).toBe(before);
		expect(sha(repo, "main")).toBe(before);
	});
});

/** Push argv is fixed except for the harness-owned temp hooks dir, which is matched by pattern. */
function expectPushArgv(argv: unknown, source: string): void {
	expect(Array.isArray(argv)).toBe(true);
	const list = argv as string[];
	expect(list).toHaveLength(8);
	expect(list.slice(0, 3)).toEqual(["-c", "push.followTags=false", "-c"]);
	expect(list[3]).toMatch(/^core\.hooksPath=.*push-hooks-/);
	expect(list.slice(4)).toEqual([
		"push",
		"--no-follow-tags",
		"origin",
		`${source}:refs/heads/main`,
	]);
}

describe("G. approved release", () => {
	it("asks exactly once and moves origin main and local main to sourceSha", async () => {
		const { repo, origin, store, runner, run } = await finishedRun();
		const source = sha(repo, `harness/${run.id}`);
		const base = sha(repo, "main");
		const human = typist();
		const result = runner.release(run.id, { approvals: human });
		expect(result).toEqual({ ok: true, sourceSha: source });
		expect(human.asks).toBe(1);
		expect(originMain(origin)).toBe(source);
		expect(sha(repo, "main")).toBe(source);
		expect(source).not.toBe(base);
		const pushed = events(store, run, "protected_op").find(
			(e) => e.data.opId === "push.main" && e.data.outcome === "ok",
		);
		expectPushArgv(pushed?.data.argv, source);
	});
});

/** Deliberately malformed ops reach the executor as untyped data; the cast is the test's point. */
const asOp = (value: unknown) => value as GitOp;

/** Real repo + DONE run + fresh grants for one transaction, with the merge already usable. */
async function withGrants() {
	const fixture = await finishedRun();
	const { repo, store, run } = fixture;
	const tx = transactionFor(repo, run);
	const grants = confirmTransaction(tx, typist());
	if (!grants) throw new Error("scripted human did not approve");
	return { ...fixture, tx, grants, deps: depsFor(repo, store, run) };
}

describe("C. alternate Git commands are rejected", () => {
	it("rejects unknown ids and extra fields without running anything", async () => {
		const { repo, origin, store, run, tx, deps } = await withGrants();
		const before = originMain(origin);
		const good = {
			id: "push.main",
			remoteUrl: tx.remoteUrl,
			sourceSha: tx.sourceSha,
			targetSha: tx.targetSha,
		};
		for (const bad of [
			{ id: "push.raw", args: ["push", "--force", "origin", "main"] },
			{ id: "git", args: ["lfs", "push", "--object-id", "origin", "abc"] },
			{ ...good, flags: ["--force-with-lease"] },
			{ ...good, args: ["--force"] },
			{ ...good, force: true },
			{ id: "merge.ff-main", sourceSha: tx.sourceSha },
			null,
			"git push origin main",
		]) {
			const result = executeGitOp(asOp(bad), deps, {});
			expect(result.ok).toBe(false);
		}
		expect(originMain(origin)).toBe(before);
		expect(sha(repo, "main")).toBe(tx.targetSha);
		const audited = events(store, run, "protected_op");
		expect(audited).toHaveLength(8);
		for (const event of audited) {
			expect(event.data.outcome).toBe("rejected");
			expect(event.data.argv).toBeNull();
		}
	});

	it("rejects a push grant used with a different sourceSha or remoteUrl", async () => {
		const { repo, origin, tx, grants, deps } = await withGrants();
		const merged = executeGitOp(
			{
				id: "merge.ff-main",
				sourceBranch: tx.sourceBranch,
				sourceSha: tx.sourceSha,
				targetSha: tx.targetSha,
			},
			deps,
			grants.get("git.merge"),
		);
		expect(merged.ok).toBe(true);
		const pushGrant = grants.get("git.push");
		const before = originMain(origin);
		const wrongSource = executeGitOp(
			{
				id: "push.main",
				remoteUrl: tx.remoteUrl,
				sourceSha: "c".repeat(40),
				targetSha: tx.targetSha,
			},
			deps,
			pushGrant,
		);
		const wrongRemote = executeGitOp(
			{
				id: "push.main",
				remoteUrl: `${tx.remoteUrl}-other`,
				sourceSha: tx.sourceSha,
				targetSha: tx.targetSha,
			},
			deps,
			pushGrant,
		);
		expect(wrongSource.ok).toBe(false);
		expect(wrongRemote.ok).toBe(false);
		expect(originMain(origin)).toBe(before);
		expect(sha(repo, "main")).toBe(tx.sourceSha);
	});

	for (const text of [
		"git push --force-with-lease origin X:main",
		"git lfs push --object-id origin 0123456789abcdef",
	]) {
		it(`blocks the worker request "${text}" as denied by policy and never executes it`, async () => {
			const { repo, origin } = makeRepo();
			const before = originMain(origin);
			expect(classifyCommand(text)).toBe("DENY");
			const { store, runner } = makeRunner(repo, new FakeAgents(text));
			const run = await runner.start("ask", { tasks: [task] });
			expect(run.stage).toBe("BLOCKED");
			expect(run.parkedReason).toContain("denied by policy");
			expect(originMain(origin)).toBe(before);
			expect(sha(repo, "main")).toBe(before);
			expect(
				command(repo, [
					"git",
					"rev-list",
					"--count",
					`main..harness/${run.id}`,
				]).trim(),
			).toBe("0");
			expect(
				store
					.events(run.id)
					.some((e) => e.data.outcome === "ok" && e.kind === "protected_op"),
			).toBe(false);
		});
	}

	it("classifies force pushes as DENY, which no grant can unlock", () => {
		expect(classifyCommand("git push --force-with-lease origin X:main")).toBe(
			"DENY",
		);
		expect(classifyCommand("git push origin main")).toBe("ASK");
	});

	it("records exactly the fixed argv for the approved push", async () => {
		const { repo, store, runner, run } = await finishedRun();
		const source = sha(repo, `harness/${run.id}`);
		expect(runner.release(run.id, { approvals: typist() }).ok).toBe(true);
		const pushes = events(store, run, "protected_op").filter(
			(e) => e.data.opId === "push.main" && e.data.outcome === "ok",
		);
		expect(pushes).toHaveLength(1);
		expectPushArgv(pushes[0]?.data.argv, source);
	});
});

describe("D. one approved transaction cannot be broadened", () => {
	const mergeOp = (tx: ReleaseTransaction): GitOp => ({
		id: "merge.ff-main",
		sourceBranch: tx.sourceBranch,
		sourceSha: tx.sourceSha,
		targetSha: tx.targetSha,
	});
	const pushOp = (tx: ReleaseTransaction): GitOp => ({
		id: "push.main",
		remoteUrl: tx.remoteUrl,
		sourceSha: tx.sourceSha,
		targetSha: tx.targetSha,
	});

	it("cannot reuse the push grant for a second push", async () => {
		const { origin, tx, grants, deps } = await withGrants();
		expect(executeGitOp(mergeOp(tx), deps, grants.get("git.merge")).ok).toBe(
			true,
		);
		expect(executeGitOp(pushOp(tx), deps, grants.get("git.push")).ok).toBe(
			true,
		);
		expect(originMain(origin)).toBe(tx.sourceSha);
		// origin main already equals sourceSha, so the replay fails closed on the preconditions too.
		const again = executeGitOp(pushOp(tx), deps, grants.get("git.push"));
		expect(again.ok).toBe(false);
		expect(originMain(origin)).toBe(tx.sourceSha);
		// The grant itself is spent: the approval gate refuses it independently of git state.
		const spent = requireApproval(
			{
				operation: "git.push",
				runId: tx.runId,
				runVersion: tx.runVersion,
				target: "main",
				sourceSha: tx.sourceSha,
				targetSha: tx.targetSha,
				remoteUrl: tx.remoteUrl,
			},
			grants.get("git.push"),
		);
		expect(spent.approved).toBe(false);
		expect(spent.reason).toContain("not issued by the trusted");
	});

	it("cannot apply the push grant to a different SHA or remote", async () => {
		const { repo, origin, store, run, tx, grants, deps } = await withGrants();
		expect(executeGitOp(mergeOp(tx), deps, grants.get("git.merge")).ok).toBe(
			true,
		);
		const before = originMain(origin);
		for (const variant of [
			{ sourceSha: tx.targetSha },
			{ remoteUrl: `${tx.remoteUrl}-other` },
		]) {
			const other = confirmTransaction(
				{ ...transactionFor(repo, run), ...variant },
				typist(),
			)?.get("git.push");
			expect(executeGitOp(pushOp(tx), deps, other).ok).toBe(false);
		}
		expect(originMain(origin)).toBe(before);
		expect(
			store
				.events(run.id)
				.some((e) => e.data.opId === "push.main" && e.data.outcome === "ok"),
		).toBe(false);
	});

	it("cannot use the push grant for merge, or the merge grant for push", async () => {
		const first = await withGrants();
		const swappedMerge = executeGitOp(
			mergeOp(first.tx),
			first.deps,
			first.grants.get("git.push"),
		);
		expect(swappedMerge.ok).toBe(false);
		if (!swappedMerge.ok)
			expect(swappedMerge.reason).toContain("operation does not match");
		expect(sha(first.repo, "main")).toBe(first.tx.targetSha);

		const second = await withGrants();
		expect(
			executeGitOp(
				mergeOp(second.tx),
				second.deps,
				second.grants.get("git.merge"),
			).ok,
		).toBe(true);
		// A fresh, unconsumed merge grant (second confirmation) offered for the push op.
		const swappedPush = executeGitOp(
			pushOp(second.tx),
			second.deps,
			confirmTransaction(second.tx, typist())?.get("git.merge"),
		);
		expect(swappedPush.ok).toBe(false);
		if (!swappedPush.ok)
			expect(swappedPush.reason).toContain("operation does not match");
		expect(originMain(second.origin)).toBe(second.tx.targetSha);
	});

	it("fails closed when main moves between confirm and execute", async () => {
		const { repo, origin, tx, grants, deps } = await withGrants();
		writeFileSync(join(repo, "moved.txt"), "moved\n");
		command(repo, ["git", "add", "moved.txt"]);
		command(repo, ["git", "commit", "-qm", "main moved"]);
		const moved = sha(repo, "main");
		const result = executeGitOp(mergeOp(tx), deps, grants.get("git.merge"));
		expect(result.ok).toBe(false);
		expect(sha(repo, "main")).toBe(moved);
		expect(originMain(origin)).toBe(tx.targetSha);
	});

	it("Runner.release fails closed when origin main moves before the push", async () => {
		const { repo, origin, runner, run } = await finishedRun();
		const human = typist();
		const result = runner.release(run.id, {
			approvals: human,
			lsRemote: () => "d".repeat(40),
		});
		expect(result.ok).toBe(false);
		expect(originMain(origin)).not.toBe(sha(repo, "main"));
	});
});

describe("E. approval cannot be fabricated", () => {
	it("rejects forged, JSON-cloned, and boolean grants at the executor", async () => {
		const { origin, tx, grants, deps } = await withGrants();
		const real = grants.get("git.merge");
		const op: GitOp = {
			id: "merge.ff-main",
			sourceBranch: tx.sourceBranch,
			sourceSha: tx.sourceSha,
			targetSha: tx.targetSha,
		};
		const forged = {
			operation: "git.merge",
			approved: true,
			source: "tty-challenge",
			runId: tx.runId,
			runVersion: tx.runVersion,
			nonce: "ABCDEF",
			target: "main",
			sourceSha: tx.sourceSha,
			targetSha: tx.targetSha,
			remoteUrl: tx.remoteUrl,
		};
		for (const fake of [
			forged,
			JSON.parse(JSON.stringify(real)),
			{ approved: true },
			true,
			"yes",
		])
			expect(executeGitOp(op, deps, fake).ok).toBe(false);
		expect(originMain(origin)).toBe(tx.targetSha);
		// the genuine grant was untouched by the failed forgeries
		expect(executeGitOp(op, deps, real).ok).toBe(true);
	});

	it("rejects {approved:true} passed to release", async () => {
		const { repo, origin, store, runner, run } = await finishedRun();
		const before = originMain(origin);
		// Not an ApprovalIO on purpose: release must not treat a flag as a human decision.
		const flag = { approved: true } as unknown as ApprovalIO;
		const result = runner.release(run.id, { approvals: flag });
		expect(result.ok).toBe(false);
		expect(originMain(origin)).toBe(before);
		expect(sha(repo, "main")).toBe(before);
		expect(
			events(store, run, "protected_op").some(
				(e) => e.data.outcome === "denied",
			),
		).toBe(true);
	});

	it("requireApproval rejects a JSON clone of a minted grant", async () => {
		const { tx, grants } = await withGrants();
		const clone = JSON.parse(JSON.stringify(grants.get("git.push")));
		const request = {
			operation: "git.push" as const,
			runId: tx.runId,
			runVersion: tx.runVersion,
			target: "main",
			sourceSha: tx.sourceSha,
			targetSha: tx.targetSha,
			remoteUrl: tx.remoteUrl,
		};
		expect(requireApproval(request, clone).approved).toBe(false);
		expect(requireApproval(request, grants.get("git.push")).approved).toBe(
			true,
		);
	});
});

describe("F. crash recovery at CHECKPOINT", () => {
	it("does not double-commit when CHECKPOINT is re-entered after the commit", async () => {
		const { repo, store, runner, run } = await finishedRun();
		expect(run.stage).toBe("DONE");
		const commits = () =>
			command(repo, [
				"git",
				"rev-list",
				"--count",
				`main..harness/${run.id}`,
			]).trim();
		expect(commits()).toBe("1");
		const head = sha(repo, `harness/${run.id}`);
		// Crash window: commit landed, but the run row still says CHECKPOINT.
		const reloaded = store.getRun(run.id);
		if (!reloaded) throw new Error("run missing");
		reloaded.stage = "CHECKPOINT";
		reloaded.currentTaskId = "task-1";
		store.saveRun(reloaded);
		const resumed = await runner.resume(run.id);
		expect(resumed.stage).toBe("DONE");
		expect(commits()).toBe("1");
		expect(sha(repo, `harness/${run.id}`)).toBe(head);
		const commitOps = events(store, run, "auto_op").filter(
			(e) => e.data.opId === "commit.task",
		);
		expect(commitOps.filter((e) => e.data.outcome === "ok")).toHaveLength(1);
		expect(commitOps.some((e) => e.data.outcome === "skipped")).toBe(true);
	});
});

const worktreeOf = (store: Store, run: RunState): string => {
	const path = store.getRun(run.id)?.worktreePath;
	if (!path) throw new Error("worktree missing");
	return path;
};
const asUser = ["-c", "user.name=t", "-c", "user.email=t@example.test"];

describe("R1. only declared files can be committed", () => {
	it("blocks an undeclared file and leaves no commit on the branch", async () => {
		const { repo } = makeRepo();
		const { store, runner } = makeRunner(
			repo,
			new FakeAgents(undefined, "stray.txt"),
		);
		const run = await runner.start("edit", { tasks: [task] });
		expect(run.stage).toBe("BLOCKED");
		expect(run.parkedReason).toContain("undeclared changed files: stray.txt");
		expect(
			command(repo, [
				"git",
				"rev-list",
				"--count",
				`main..harness/${run.id}`,
			]).trim(),
		).toBe("0");
		expect(
			events(store, run, "auto_op").some((e) => e.data.outcome === "ok"),
		).toBe(false);
	});

	it("accepts a glob that covers every changed path", async () => {
		const { repo } = makeRepo();
		const { runner } = makeRunner(repo, new FakeAgents(undefined, "stray.txt"));
		const run = await runner.start("edit", {
			tasks: [{ ...task, files: ["*.txt"] }],
		});
		expect(run.stage).toBe("DONE");
	});
});

describe("R3. the committed tree is the tree the reviewer approved", () => {
	it("refuses the auto commit when the tree changed after review approval", async () => {
		const { repo } = makeRepo();
		const { store, runner } = makeRunner(repo);
		const run = await runner.start("edit", {
			tasks: [{ ...task, needsBuild: false }],
		});
		expect(run.stage).toBe("DONE");
		const wt = worktreeOf(store, run);
		// Undo the commit but keep the approved edit, then change the tree after approval.
		command(wt, ["git", "reset", "-q", "--mixed", "HEAD~1"]);
		writeFileSync(join(wt, "tracked.txt"), "tampered after approval\n");
		const reloaded = store.getRun(run.id);
		if (!reloaded) throw new Error("run missing");
		reloaded.stage = "CHECKPOINT";
		reloaded.currentTaskId = "task-1";
		store.saveRun(reloaded);
		const resumed = await runner.resume(run.id);
		expect(resumed.stage).toBe("BLOCKED");
		expect(resumed.parkedReason).toContain("reviewer approved");
		expect(
			command(repo, [
				"git",
				"rev-list",
				"--count",
				`main..harness/${run.id}`,
			]).trim(),
		).toBe("0");
	});
});

describe("R4. release is pinned to the recorded task commits", () => {
	it("denies a branch that gained an extra commit after DONE, without asking", async () => {
		const { repo, origin, store, runner, run } = await finishedRun();
		const wt = worktreeOf(store, run);
		writeFileSync(join(wt, "tracked.txt"), "sneaky\n");
		command(wt, ["git", ...asUser, "commit", "-q", "-a", "-m", "extra"]);
		const before = originMain(origin);
		const human = typist();
		const result = runner.release(run.id, { approvals: human });
		expect(result.ok).toBe(false);
		expect(result.reason).toContain("not the last recorded task commit");
		expect(human.asks).toBe(0);
		expect(originMain(origin)).toBe(before);
		expect(sha(repo, "main")).toBe(before);
	});

	it("records the commit sha on the commit.task event", async () => {
		const { repo, store, run } = await finishedRun();
		const ok = events(store, run, "auto_op").filter(
			(e) => e.data.opId === "commit.task" && e.data.outcome === "ok",
		);
		expect(ok.at(-1)?.data.sha).toBe(sha(repo, `harness/${run.id}`));
	});
});

describe("R5. the push URL must be the approved remote", () => {
	it("rejects a pushurl pointing at a different bare repo", async () => {
		const { repo, origin, tx, grants, deps } = await withGrants();
		const other = mkdtempSync(join(tmpdir(), "other-bare-"));
		roots.push(other);
		command(other, ["git", "init", "-q", "--bare", "."]);
		command(repo, ["git", "config", "remote.origin.pushurl", other]);
		const merged = executeGitOp(
			{
				id: "merge.ff-main",
				sourceBranch: tx.sourceBranch,
				sourceSha: tx.sourceSha,
				targetSha: tx.targetSha,
			},
			deps,
			grants.get("git.merge"),
		);
		expect(merged.ok).toBe(true);
		const before = originMain(origin);
		const pushed = executeGitOp(
			{
				id: "push.main",
				remoteUrl: tx.remoteUrl,
				sourceSha: tx.sourceSha,
				targetSha: tx.targetSha,
			},
			deps,
			grants.get("git.push"),
		);
		expect(pushed.ok).toBe(false);
		if (!pushed.ok) expect(pushed.reason).toContain("push URL");
		expect(originMain(origin)).toBe(before);
	});
});

describe("R6. shell metacharacters and DENY patterns", () => {
	it("never classifies a chained command as AUTO", () => {
		expect(classifyCommand("git status && git push origin main")).toBe("ASK");
		expect(classifyCommand("git add -- a.txt; git push origin main")).not.toBe(
			"AUTO",
		);
		expect(classifyCommand("git status\ngit push origin main")).toBe("ASK");
		expect(classifyCommand("git status $(id)")).toBe("ASK");
	});
	it("lets DENY win over the metacharacter rule", () => {
		expect(classifyCommand("test; git lfs push --object-id origin X")).toBe(
			"DENY",
		);
		expect(classifyCommand("git diff --output=/tmp/x")).toBe("DENY");
		expect(classifyCommand("git push origin +main")).toBe("DENY");
	});
	it("keeps a plain push as ASK", () => {
		expect(classifyCommand("git push origin main")).toBe("ASK");
	});
});

describe("R8. recovering from a refused push", () => {
	it("asks once for a push-only transaction, then reports already released", async () => {
		const { repo, origin, store, runner, run } = await finishedRun();
		const source = sha(repo, `harness/${run.id}`);
		const first = runner.release(run.id, {
			approvals: typist(),
			lsRemote: () => "d".repeat(40),
		});
		expect(first.ok).toBe(false);
		expect(sha(repo, "main")).toBe(source); // merge landed, push refused
		const oldOrigin = originMain(origin);
		expect(oldOrigin).not.toBe(source);

		let prompt = "";
		const human = {
			asks: 0,
			ask(text: string): string | undefined {
				human.asks += 1;
				prompt = text;
				return text.match(/Type ([0-9A-F]{6}) /)?.[1];
			},
		};
		const second = runner.release(run.id, { approvals: human });
		expect(second).toEqual({ ok: true, sourceSha: source });
		expect(human.asks).toBe(1);
		expect(prompt).toContain("operations: git.push");
		expect(prompt).not.toContain("git.merge");
		expect(originMain(origin)).toBe(source);

		const third = runner.release(run.id, { approvals: typist() });
		expect(third.ok).toBe(false);
		expect(third.reason).toBe("already released");
		expect(
			events(store, run, "protected_op").filter(
				(e) => e.data.opId === "push.main" && e.data.outcome === "ok",
			),
		).toHaveLength(1);
	});
});

describe("R10. hooks and LFS", () => {
	it("lists the LFS objects being published in the transaction prompt", async () => {
		const { repo, runner, run } = await finishedRun();
		let prompt = "";
		const result = runner.release(run.id, {
			approvals: {
				ask: (text: string) => {
					prompt = text;
					return text.match(/Type ([0-9A-F]{6}) /)?.[1];
				},
			},
		});
		expect(result.ok).toBe(true);
		expect(prompt).toContain("lfs objects published:");
		expect(sha(repo, "main")).toBe(sha(repo, `harness/${run.id}`));
	});

	it("refuses to push when pre-push is not the stock git-lfs hook", async () => {
		const { repo, origin, tx, grants, deps } = await withGrants();
		writeFileSync(
			join(repo, ".git", "hooks", "pre-push"),
			"#!/bin/sh\nexit 0\n",
		);
		expect(
			executeGitOp(
				{
					id: "merge.ff-main",
					sourceBranch: tx.sourceBranch,
					sourceSha: tx.sourceSha,
					targetSha: tx.targetSha,
				},
				deps,
				grants.get("git.merge"),
			).ok,
		).toBe(true);
		const before = originMain(origin);
		const pushed = executeGitOp(
			{
				id: "push.main",
				remoteUrl: tx.remoteUrl,
				sourceSha: tx.sourceSha,
				targetSha: tx.targetSha,
			},
			deps,
			grants.get("git.push"),
		);
		expect(pushed.ok).toBe(false);
		if (!pushed.ok) expect(pushed.reason).toContain("pre-push hook");
		expect(originMain(origin)).toBe(before);
	});

	it("commits with hooks disabled", async () => {
		const { repo } = makeRepo();
		writeFileSync(
			join(repo, ".git", "hooks", "pre-commit"),
			"#!/bin/sh\nexit 1\n",
			{ mode: 0o755 },
		);
		const { runner } = makeRunner(repo);
		const run = await runner.start("edit", { tasks: [task] });
		expect(run.stage).toBe("DONE");
	});
});

describe("N1. a rename cannot hide an undeclared deletion", () => {
	it("blocks a git mv of an undeclared file onto a declared one, with no commit", async () => {
		const { repo } = makeRepo();
		writeFileSync(join(repo, "undeclared.txt"), "keep me\n");
		command(repo, ["git", "add", "undeclared.txt"]);
		command(repo, ["git", "commit", "-qm", "add undeclared"]);
		command(repo, ["git", "push", "-q", "origin", "main"]);
		const renamer: AgentPort = {
			async run(request: AgentRequest): Promise<AgentResult> {
				if (request.role === "implementer") {
					command(request.cwd, ["git", "mv", "undeclared.txt", "declared.txt"]);
					return { status: "idle", agentId: "implementer" };
				}
				return {
					status: "idle",
					agentId: "reviewer",
					structured: { verdict: "approve", findings: [] },
				};
			},
			async stop(): Promise<void> {},
		};
		const { runner } = makeRunner(repo, renamer);
		const run = await runner.start("rename", {
			tasks: [{ ...task, files: ["declared.txt"], needsBuild: false }],
		});
		expect(run.stage).toBe("BLOCKED");
		expect(run.parkedReason).toContain("undeclared.txt");
		expect(
			command(repo, [
				"git",
				"rev-list",
				"--count",
				`main..harness/${run.id}`,
			]).trim(),
		).toBe("0");
	});
});

describe("N3. no repo hook runs during merge or push", () => {
	it("does not run reference-transaction or post-merge hooks during release", async () => {
		const { repo, origin, runner, run } = await finishedRun();
		const markerDir = mkdtempSync(join(tmpdir(), "hook-marker-"));
		roots.push(markerDir);
		const marker = join(markerDir, "ran");
		for (const name of [
			"reference-transaction",
			"post-merge",
			"post-checkout",
		]) {
			const file = join(repo, ".git", "hooks", name);
			writeFileSync(file, `#!/bin/sh\necho ${name} >> "${marker}"\n`);
			chmodSync(file, 0o755);
		}
		// Control: the hook really fires on a plain ref update, so an absent marker is meaningful.
		command(repo, ["git", "update-ref", "refs/tmp/control", "HEAD"]);
		expect(existsSync(marker)).toBe(true);
		rmSync(marker);
		const result = runner.release(run.id, { approvals: typist() });
		expect(result.ok).toBe(true);
		expect(originMain(origin)).toBe(sha(repo, `harness/${run.id}`));
		expect(existsSync(marker)).toBe(false);
	});
});
