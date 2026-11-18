import { afterEach, describe, expect, it } from "bun:test";
import { existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import type { ContextBuilder } from "../src/context.ts";
import type { Gates } from "../src/gates.ts";
import { Runner } from "../src/graph.ts";
import { Store } from "../src/store.ts";
import type {
	AgentPort,
	AgentRequest,
	AgentResult,
	GateResult,
	HarnessConfig,
	Role,
	TaskSpec,
} from "../src/types.ts";

const stores: Store[] = [];
const dirs: string[] = [];

afterEach(() => {
	for (const store of stores.splice(0)) store.close();
	for (const dir of dirs.splice(0)) rmSync(dir, { recursive: true, force: true });
});

function tmp(): string {
	const dir = mkdtempSync(join(tmpdir(), "psl-lengthstop-"));
	dirs.push(dir);
	return dir;
}

function config(maxAttempts: number): HarnessConfig {
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

function task(maxAttempts: number): TaskSpec {
	return {
		id: "task-1",
		title: "Task",
		description: "Do the task",
		dependsOn: [],
		acceptance: ["It is complete"],
		files: ["docs/**"],
		needsBuild: false,
		maxAttempts,
		budgetMs: 1000,
	};
}

const okGate = (gate: GateResult["gate"]): GateResult => ({
	gate,
	outcome: "SUCCESS",
	exitCode: 0,
	durationMs: 1,
	command: ["fake"],
	logPath: "x.log",
});

const gates: Gates = {
	preflight: async () => okGate("PREFLIGHT"),
	build: async () => okGate("BUILD"),
	test: async () => okGate("TEST"),
};

/** Real handoff files under a tmp artifact dir, like RepoContextBuilder. */
class TmpContext implements ContextBuilder {
	constructor(private readonly dir: string) {}
	async forPlanner(): Promise<string> {
		return "planner";
	}
	async forTask(_run: unknown, t: { id: string }, role: Role): Promise<string> {
		return `${role} ${t.id}`;
	}
	writeHandoff(
		run: { id: string },
		t: { id: string; attempt: number },
		data: { role: Role },
	): string {
		const dir = join(this.dir, "runs", run.id, t.id);
		mkdirSync(dir, { recursive: true });
		const path = join(dir, `handoff-${t.attempt}-${data.role}.md`);
		writeFileSync(path, "handoff\n");
		return path;
	}
}

function setup(
	maxAttempts: number,
	implementer: (n: number) => AgentResult,
	reviewer: (n: number) => AgentResult = () => ({
		status: "idle",
		agentId: "rev",
		structured: { verdict: "approve", findings: [] },
	}),
) {
	const dir = tmp();
	const store = new Store(join(dir, "state.db"));
	stores.push(store);
	const prompts: string[] = [];
	const reviewPrompts: string[] = [];
	let impl = 0;
	const agents: AgentPort = {
		async run(request: AgentRequest): Promise<AgentResult> {
			if (request.role === "reviewer") {
				reviewPrompts.push(request.prompt);
				return reviewer(reviewPrompts.length);
			}
			prompts.push(request.prompt);
			impl += 1;
			return implementer(impl);
		},
		async stop() {},
	};
	const runner = new Runner({
		store,
		agents,
		gates,
		context: new TmpContext(dir),
		config: config(maxAttempts),
		repoRoot: process.cwd(),
		owner: "test-owner",
	});
	return { runner, store, prompts, reviewPrompts, dir };
}

const truncated = (n: number): AgentResult => ({
	status: "idle",
	agentId: `w${n}`,
	text: `partial work ${n}`,
	stopReason: "length",
});

describe("PSL.lengthStop", () => {
	it("treats idle + stopReason length as AGENT_LENGTH_STOP, not PASSED", async () => {
		const { runner, store } = setup(1, truncated);
		const run = await runner.start("len", { tasks: [task(1)] });
		const t = store.getTasks(run.id)[0];
		expect(t?.status).toBe("FAILED");
		expect(t?.lastFailure?.cls).toBe("AGENT_LENGTH_STOP");
	});

	it("retry prompt has OUTPUT BUDGET + partial path and differs from the first", async () => {
		const { runner, store, prompts } = setup(2, (n) =>
			n === 1 ? truncated(n) : { status: "idle", agentId: "ok", text: "done" },
		);
		const run = await runner.start("len", { tasks: [task(2)] });
		expect(run.stage).toBe("DONE");
		expect(prompts).toHaveLength(2);
		expect(prompts[0]).not.toContain("OUTPUT BUDGET");
		expect(prompts[1]).toContain("OUTPUT BUDGET");
		expect(prompts[1]).not.toBe(prompts[0]);
		const partial = prompts[1]?.match(/saved partial output at (\S+)\.$/)?.[1];
		expect(partial).toBeDefined();
		expect(readFileSync(partial as string, "utf8")).toContain("partial work 1");
		expect(store.getTasks(run.id)[0]?.status).toBe("PASSED");
	});

	it("keeps one partial file per attempt and never overwrites", async () => {
		const { runner, store, dir } = setup(3, truncated);
		const run = await runner.start("len", { tasks: [task(3)] });
		const base = join(dir, "runs", run.id, "task-1");
		for (const n of [1, 2, 3]) {
			const path = join(base, `partial-task-1-attempt${n}.md`);
			expect(existsSync(path)).toBe(true);
			expect(readFileSync(path, "utf8")).toContain(`partial work ${n}`);
		}
		expect(store.getTasks(run.id)[0]?.attempt).toBe(3);
	});

	it("fails the task and run with AGENT_LENGTH_STOP on exhaustion", async () => {
		const { runner, store } = setup(2, truncated);
		const run = await runner.start("len", { tasks: [task(2)] });
		expect(run.stage).toBe("FAILED");
		const t = store.getTasks(run.id)[0];
		expect(t?.status).toBe("FAILED");
		expect(t?.lastFailure?.cls).toBe("AGENT_LENGTH_STOP");
	});

	it("still passes a normal idle result without stopReason", async () => {
		const { runner, store, prompts } = setup(1, () => ({
			status: "idle",
			agentId: "ok",
			text: "done",
		}));
		const run = await runner.start("ok", { tasks: [task(1)] });
		expect(run.stage).toBe("DONE");
		expect(store.getTasks(run.id)[0]?.status).toBe("PASSED");
		expect(prompts[0]).not.toContain("OUTPUT BUDGET");
	});

	it("retries only the reviewer when the reviewer hits the cap (REV-2)", async () => {
		const { runner, store, prompts, reviewPrompts } = setup(
			2,
			() => ({ status: "idle", agentId: "ok", text: "done" }),
			(n) =>
				n === 1
					? { status: "idle", agentId: "rev", text: '{"verdict":"appr', stopReason: "length" }
					: { status: "idle", agentId: "rev", structured: { verdict: "approve", findings: [] } },
		);
		const run = await runner.start("len", { tasks: [task(2)] });
		expect(run.stage).toBe("DONE");
		expect(prompts).toHaveLength(1);
		expect(reviewPrompts).toHaveLength(2);
		expect(reviewPrompts[0]).not.toContain("OUTPUT BUDGET");
		expect(reviewPrompts[1]).toContain("output-token cap");
		expect(reviewPrompts[1]).toContain("OUTPUT BUDGET");
		const t = store.getTasks(run.id)[0];
		expect(t?.status).toBe("PASSED");
		expect(t?.attempt).toBe(1);
	});
});
