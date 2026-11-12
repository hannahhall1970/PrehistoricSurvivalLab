import {
	existsSync,
	mkdirSync,
	readFileSync,
	renameSync,
	writeFileSync,
} from "node:fs";
import { dirname, join, resolve } from "node:path";
import { RepoContextBuilder } from "./context.ts";
import { loadConfig } from "./config.ts";
import { CommandGates, type Gates } from "./gates.ts";
import { Runner } from "./graph.ts";
import { BuildLock } from "./lock.ts";
import { PaseoAgentPort } from "./paseo.ts";
import { Store } from "./store.ts";
import type {
	AgentPort,
	AgentRequest,
	AgentResult,
	GateResult,
	HarnessConfig,
} from "./types.ts";
import { BLOCKING_FAILURES } from "./types.ts";
import { TtyApprovalSource } from "./approval.ts";

const usage = `bun harness/src/cli.ts <start "objective" | status [runId] | resume <runId> [--approve] | stop <runId> | logs <runId> [--task id] | gate <preflight|build|test>> [--fake]`;

function fakeDelay(): number {
	const value = Number(process.env.HARNESS_FAKE_DELAY_MS ?? 0);
	return Number.isFinite(value) && value > 0 ? value : 0;
}
/** Approval comes only from the controlling-terminal challenge; grant-like flags are refused. */
function rejectGrantFlags(args: string[]): string | undefined {
	const flag = args.find((arg) => /^--approve-(op|target)(=|$)/.test(arg));
	return flag
		? `${flag} is not accepted: approvals are confirmed interactively on the controlling terminal (resume <runId> --approve)`
		: undefined;
}

function sleep(ms: number, signal?: AbortSignal): Promise<boolean> {
	return new Promise((resolvePromise) => {
		if (signal?.aborted) {
			resolvePromise(false);
			return;
		}
		let timer: ReturnType<typeof setTimeout> | undefined;
		const abort = () => {
			if (timer) clearTimeout(timer);
			signal?.removeEventListener("abort", abort);
			resolvePromise(false);
		};
		timer = setTimeout(() => {
			signal?.removeEventListener("abort", abort);
			resolvePromise(true);
		}, ms);
		signal?.addEventListener("abort", abort, { once: true });
	});
}

type FakeRecord = { count: number; calls: number; agentId: string };

type FakeState = Record<string, FakeRecord>;

class FakeAgentPort implements AgentPort {
	constructor(private readonly filename: string) {}

	async run(
		request: AgentRequest,
		signal?: AbortSignal,
		hooks?: { onCreated?(agentId: string): void },
	): Promise<AgentResult> {
		const state = this.read();
		let record = state[request.idempotencyKey];
		const created = !record;
		record ??= {
			count: 1,
			calls: 0,
			agentId: `fake-${this.safeId(request.idempotencyKey)}`,
		};
		record.calls = (record.calls ?? 0) + 1;
		state[request.idempotencyKey] = record;
		this.write(state);
		hooks?.onCreated?.(record.agentId);
		if (created && fakeDelay() > 0 && !(await sleep(fakeDelay(), signal))) {
			return {
				status: "error",
				agentId: record.agentId,
				error: "fake agent aborted",
			};
		}
		if (signal?.aborted) {
			return {
				status: "error",
				agentId: record.agentId,
				error: "fake agent aborted",
			};
		}
		if (request.role === "planner") {
			return {
				status: "idle",
				agentId: record.agentId,
				structured: {
					tasks: [
						{
							id: "fake-task",
							title: "Fake worker task",
							description: "Exercise the harness end to end",
							dependsOn: [],
							acceptance: ["The harness reaches a reviewed checkpoint"],
							files: ["Source/**"],
							needsBuild: false,
						},
					],
				},
			};
		}
		if (request.role === "reviewer") {
			return {
				status: "idle",
				agentId: record.agentId,
				structured: { verdict: "approve", findings: [] },
			};
		}
		return {
			status: "idle",
			agentId: record.agentId,
			text: JSON.stringify({ summary: "fake worker turn ended" }),
		};
	}

	async stop(_agentId: string): Promise<void> {}

	private read(): FakeState {
		if (!existsSync(this.filename)) return {};
		try {
			const value = JSON.parse(readFileSync(this.filename, "utf8"));
			return typeof value === "object" && value !== null
				? (value as FakeState)
				: {};
		} catch {
			return {};
		}
	}

	private write(state: FakeState): void {
		mkdirSync(dirname(this.filename), { recursive: true });
		const temporary = `${this.filename}.${process.pid}.tmp`;
		writeFileSync(temporary, `${JSON.stringify(state, null, "\t")}\n`);
		renameSync(temporary, this.filename);
	}

	private safeId(value: string): string {
		return value.replace(/[^A-Za-z0-9_-]/g, "_").slice(-80);
	}
}

class FakeGates implements Gates {
	constructor(private readonly repoRoot: string) {}

	preflight(): Promise<GateResult> {
		return Promise.resolve(this.result("PREFLIGHT"));
	}

	build(): Promise<GateResult> {
		return Promise.resolve(this.result("BUILD"));
	}

	test(): Promise<GateResult> {
		return Promise.resolve(this.result("TEST"));
	}

	private result(gate: GateResult["gate"]): GateResult {
		return {
			gate,
			outcome: "SUCCESS",
			exitCode: 0,
			durationMs: 0,
			command: ["fake", gate.toLowerCase()],
			logPath: join(
				this.repoRoot,
				".harness",
				"logs",
				`fake-${gate.toLowerCase()}.log`,
			),
		};
	}
}

function configFor(repoRoot: string): HarnessConfig {
	return loadConfig(repoRoot);
}

function makeRunner(
	repoRoot: string,
	fake: boolean,
): {
	runner: Runner;
	store: Store;
	agents: AgentPort;
} {
	const config = configFor(repoRoot);
	const store = new Store(join(repoRoot, ".harness", "state.db"));
	const agents = fake
		? new FakeAgentPort(join(repoRoot, ".harness", "fake-agents.json"))
		: new PaseoAgentPort({ config });
	const gates: Gates = fake
		? new FakeGates(repoRoot)
		: new CommandGates({
				repoRoot,
				config,
				logDir: join(repoRoot, ".harness", "logs"),
				lock: new BuildLock(join(repoRoot, ".harness", "ue-build.lock")),
			});
	const context = new RepoContextBuilder({
		repoRoot,
		artifactDir: join(repoRoot, ".harness"),
	});
	return {
		runner: new Runner({
			store,
			agents,
			gates,
			context,
			config,
			repoRoot,
			owner: `cli:${process.pid}`,
		}),
		store,
		agents,
	};
}

function printRun(store: Store, runId?: string): void {
	const run = runId ? store.getRun(runId) : store.listRuns()[0];
	if (!run) {
		console.log("No runs");
		return;
	}
	console.log(`run ${run.id} stage=${run.stage} version=${run.version}`);
	if (run.parkedReason) console.log(`reason: ${run.parkedReason}`);
	const repoLease = store.getRunLease("repo-writer");
	if (repoLease)
		console.log(
			`repo-writer: ${repoLease.owner} (pid ${repoLease.pid ?? "?"})`,
		);
	const lease = store.getRunLease(run.id);
	if (lease) console.log(`lease: ${lease.owner} (pid ${lease.pid ?? "?"})`);
	for (const task of store.getTasks(run.id)) {
		const failure = task.lastFailure
			? ` failure=${task.lastFailure.cls}:${task.lastFailure.summary}`
			: "";
		console.log(
			`${task.id}\t${task.status}\tattempt=${task.attempt}${failure}`,
		);
	}
}

function printLogs(store: Store, runId: string, taskId?: string): void {
	const events = store
		.events(runId)
		.filter((event) => !taskId || event.taskId === taskId);
	for (const event of events) {
		console.log(
			`${event.seq} ${event.at} ${event.stage} ${event.kind}${event.taskId ? ` task=${event.taskId}` : ""}`,
		);
		const data = event.data;
		for (const key of ["handoffPath", "logPath"]) {
			const path = typeof data[key] === "string" ? data[key] : undefined;
			if (!path) continue;
			console.log(`${key}: ${path}`);
			if (key === "logPath" && existsSync(path)) {
				const lines = readFileSync(path, "utf8").split(/\r?\n/);
				console.log(lines.slice(-40).join("\n"));
			}
		}
		if (data.failure) console.log(`failure: ${JSON.stringify(data.failure)}`);
	}
}

function exitFor(run: { stage: string }): number {
	if (run.stage === "DONE") return 0;
	if (run.stage === "BLOCKED" || run.stage === "HUMAN_APPROVAL") return 2;
	if (run.stage === "STOPPED") return 3;
	if (run.stage === "FAILED") return 1;
	return 0;
}

export async function main(argv = process.argv.slice(2)): Promise<number> {
	const fake =
		argv.includes("--fake") || process.env.HARNESS_FAKE_AGENT === "1";
	const args = argv.filter((arg) => arg !== "--fake");
	const grantError = rejectGrantFlags(args);
	if (grantError) {
		console.error(grantError);
		return 1;
	}
	const command = args[0];
	const repoRoot = resolve(process.cwd());
	if (!command) {
		console.error(usage);
		return 1;
	}
	if (command === "status") {
		const store = new Store(join(repoRoot, ".harness", "state.db"));
		try {
			printRun(store, args[1]);
			return 0;
		} finally {
			store.close();
		}
	}
	if (command === "logs" && args[1]) {
		const store = new Store(join(repoRoot, ".harness", "state.db"));
		try {
			const taskFlag = args.indexOf("--task");
			printLogs(store, args[1], taskFlag >= 0 ? args[taskFlag + 1] : undefined);
			return 0;
		} finally {
			store.close();
		}
	}
	if (command === "gate" && args[1]) {
		const config = configFor(repoRoot);
		const gates: Gates = fake
			? new FakeGates(repoRoot)
			: new CommandGates({
					repoRoot,
					config,
					logDir: join(repoRoot, ".harness", "logs"),
					lock: new BuildLock(join(repoRoot, ".harness", "ue-build.lock")),
				});
		const result =
			args[1] === "preflight"
				? await gates.preflight()
				: args[1] === "build"
					? await gates.build()
					: await gates.test();
		console.log(JSON.stringify(result, null, "\t"));
		return result.outcome === "SUCCESS"
			? 0
			: result.failure && BLOCKING_FAILURES.has(result.failure.cls)
				? 2
				: 1;
	}
	if (command === "start" && args.length >= 2) {
		const { runner, store, agents } = makeRunner(repoRoot, fake);
		try {
			const run = await runner.start(args.slice(1).join(" "));
			console.log(`run ${run.id} stage=${run.stage}`);
			if (run.parkedReason) console.log(`reason: ${run.parkedReason}`);
			return exitFor(run);
		} finally {
			if (agents instanceof PaseoAgentPort) await agents.close();
			store.close();
		}
	}
	if (command === "resume" && args[1]) {
		const { runner, store, agents } = makeRunner(repoRoot, fake);
		try {
			const run = await runner.resume(args[1], {
				approve: args.includes("--approve"),
				approvals: new TtyApprovalSource(),
			});
			console.log(`run ${run.id} stage=${run.stage}`);
			if (run.parkedReason) console.log(`reason: ${run.parkedReason}`);
			return exitFor(run);
		} finally {
			if (agents instanceof PaseoAgentPort) await agents.close();
			store.close();
		}
	}
	if (command === "stop" && args[1]) {
		const { runner, store, agents } = makeRunner(repoRoot, fake);
		try {
			runner.requestStop(args[1]);
			printRun(store, args[1]);
			return 0;
		} finally {
			if (agents instanceof PaseoAgentPort) await agents.close();
			store.close();
		}
	}
	console.error(usage);
	return 1;
}

if (import.meta.main) process.exitCode = await main();
