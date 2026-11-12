import {
	PARKED_STAGES,
	TERMINAL_STAGES,
	type AgentPort,
	type AgentResult,
	type Failure,
	type HarnessConfig,
	type Outcome,
	type PlanOutput,
	type ReviewVerdict,
	type Role,
	type RunState,
	type Stage,
	type TaskSpec,
	type TaskState,
} from "./types.ts";
import type { ContextBuilder } from "./context.ts";
import type { Gates } from "./gates.ts";
import type { Store } from "./store.ts";

export type EdgeTable = Record<Stage, Partial<Record<Outcome, Stage>>>;

export const EDGE_TABLE: EdgeTable = {
	INTAKE: { SUCCESS: "PREFLIGHT" },
	PREFLIGHT: { SUCCESS: "PLAN" },
	PLAN: { SUCCESS: "TASK_CONTRACT", FAILURE: "PLAN", TIMEOUT: "PLAN" },
	TASK_CONTRACT: { SUCCESS: "EXECUTE" },
	EXECUTE: { SUCCESS: "BUILD", FAILURE: "EXECUTE", TIMEOUT: "EXECUTE" },
	BUILD: { SUCCESS: "TEST", FAILURE: "EXECUTE", TIMEOUT: "EXECUTE" },
	TEST: {
		SUCCESS: "INDEPENDENT_REVIEW",
		FAILURE: "EXECUTE",
		TIMEOUT: "EXECUTE",
	},
	INDEPENDENT_REVIEW: {
		SUCCESS: "CHECKPOINT",
		FAILURE: "EXECUTE",
		TIMEOUT: "EXECUTE",
	},
	CHECKPOINT: { SUCCESS: "NEXT_TASK" },
	NEXT_TASK: { SUCCESS: "TASK_CONTRACT" },
	DONE: {},
	BLOCKED: {},
	FAILED: {},
	HUMAN_APPROVAL: {},
	STOPPED: {},
};

export const EDGES = EDGE_TABLE;

export function transition(
	stage: Stage,
	outcome: Outcome,
	ctx: {
		attempt: number;
		maxAttempts: number;
		hasMoreTasks: boolean;
		failure?: Failure;
	},
): Stage {
	if (TERMINAL_STAGES[stage] || PARKED_STAGES[stage]) return stage;
	if (
		ctx.failure?.cls === "TOOLCHAIN_MISSING" &&
		(stage === "PREFLIGHT" || stage === "BUILD" || stage === "TEST")
	) {
		return "BLOCKED";
	}
	if (ctx.failure?.cls === "PERMISSION_REQUIRED") return "HUMAN_APPROVAL";
	if (outcome === "BLOCKED") return "BLOCKED";
	if (outcome === "NEEDS_APPROVAL") return "HUMAN_APPROVAL";
	if (stage === "NEXT_TASK" && outcome === "SUCCESS") {
		return ctx.hasMoreTasks ? "TASK_CONTRACT" : "DONE";
	}
	if (outcome === "FAILURE" || outcome === "TIMEOUT") {
		if (stage === "PLAN") {
			return ctx.attempt < ctx.maxAttempts ? "PLAN" : "FAILED";
		}
		if (
			stage === "EXECUTE" ||
			stage === "BUILD" ||
			stage === "TEST" ||
			stage === "INDEPENDENT_REVIEW"
		) {
			return ctx.attempt < ctx.maxAttempts ? "EXECUTE" : "FAILED";
		}
	}
	return EDGE_TABLE[stage][outcome] ?? "FAILED";
}

export interface PlanValidation {
	tasks?: TaskState[];
	error?: string;
}

function record(value: unknown): value is Record<string, unknown> {
	return typeof value === "object" && value !== null && !Array.isArray(value);
}

function strings(value: unknown): value is string[] {
	return (
		Array.isArray(value) && value.every((entry) => typeof entry === "string")
	);
}

function requiredStrings(value: unknown, field: string): string[] | string {
	if (!strings(value)) return `${field} must be an array of strings`;
	if (value.some((entry) => entry.trim().length === 0)) {
		return `${field} must not contain empty strings`;
	}
	return value;
}

function taskState(
	spec: Record<string, unknown>,
	config: HarnessConfig,
): TaskState | string {
	if (typeof spec.id !== "string" || !/^[A-Za-z0-9_-]+$/.test(spec.id)) {
		return "task id must match [A-Za-z0-9_-]+";
	}
	for (const field of ["title", "description"]) {
		if (typeof spec[field] !== "string" || spec[field].trim().length === 0) {
			return `${field} must be non-empty`;
		}
	}
	const dependsOn = requiredStrings(spec.dependsOn, "dependsOn");
	if (typeof dependsOn === "string") return dependsOn;
	const acceptance = requiredStrings(spec.acceptance, "acceptance");
	if (typeof acceptance === "string" || acceptance.length === 0) {
		return typeof acceptance === "string"
			? acceptance
			: "acceptance must be non-empty";
	}
	const files = requiredStrings(spec.files, "files");
	if (typeof files === "string") return files;
	if (typeof spec.needsBuild !== "boolean") return "needsBuild must be boolean";
	const maxAttempts = spec.maxAttempts ?? config.limits.defaultMaxAttempts;
	const budgetMs = spec.budgetMs ?? config.limits.defaultBudgetMs;
	if (
		typeof maxAttempts !== "number" ||
		!Number.isInteger(maxAttempts) ||
		maxAttempts < 1
	) {
		return "maxAttempts must be an integer >= 1";
	}
	if (
		typeof budgetMs !== "number" ||
		!Number.isFinite(budgetMs) ||
		budgetMs <= 0
	) {
		return "budgetMs must be > 0";
	}
	return {
		id: spec.id as string,
		title: spec.title as string,
		description: spec.description as string,
		dependsOn,
		acceptance,
		files,
		needsBuild: spec.needsBuild,
		maxAttempts,
		budgetMs,
		status: "PENDING",
		attempt: 0,
	};
}

function validateTasks(input: unknown, config: HarnessConfig): PlanValidation {
	if (!Array.isArray(input)) return { error: "plan.tasks must be an array" };
	if (input.length > config.limits.maxTasksPerRun) {
		return {
			error: `plan has ${input.length} tasks; max is ${config.limits.maxTasksPerRun}`,
		};
	}
	if (input.length === 0) return { error: "plan.tasks must not be empty" };
	const tasks: TaskState[] = [];
	for (const value of input) {
		if (!record(value)) return { error: "each task must be an object" };
		const result = taskState(value, config);
		if (typeof result === "string") return { error: result };
		tasks.push(result);
	}
	const ids = new Set<string>();
	for (const task of tasks) {
		if (ids.has(task.id)) return { error: `duplicate task id: ${task.id}` };
		ids.add(task.id);
	}
	for (const task of tasks) {
		for (const dependency of task.dependsOn) {
			if (!ids.has(dependency)) {
				return {
					error: `task ${task.id} depends on unknown task ${dependency}`,
				};
			}
		}
	}
	const visiting = new Set<string>();
	const visited = new Set<string>();
	const byId = new Map(tasks.map((task) => [task.id, task]));
	const visit = (id: string): boolean => {
		if (visiting.has(id)) return false;
		if (visited.has(id)) return true;
		visiting.add(id);
		const task = byId.get(id);
		if (!task || task.dependsOn.some((dependency) => !visit(dependency)))
			return false;
		visiting.delete(id);
		visited.add(id);
		return true;
	};
	for (const task of tasks) {
		if (!visit(task.id)) return { error: "task dependencies contain a cycle" };
	}
	return { tasks };
}

export function validatePlan(
	output: unknown,
	config: HarnessConfig,
): PlanValidation {
	if (!record(output)) return { error: "planner output must be an object" };
	return validateTasks(output.tasks, config);
}

const PLAN_SCHEMA: Record<string, unknown> = {
	type: "object",
	required: ["tasks"],
	properties: {
		tasks: {
			type: "array",
			items: {
				type: "object",
				required: [
					"id",
					"title",
					"description",
					"dependsOn",
					"acceptance",
					"files",
					"needsBuild",
				],
				additionalProperties: false,
			},
		},
	},
};

const REVIEW_SCHEMA: Record<string, unknown> = {
	type: "object",
	required: ["verdict", "findings"],
	properties: {
		verdict: { type: "string", enum: ["approve", "reject"] },
		findings: { type: "array" },
	},
};

function parseStructured<T>(value: unknown, text?: string): T | undefined {
	if (record(value) || Array.isArray(value)) return value as T;
	if (typeof value === "string") {
		try {
			return JSON.parse(value) as T;
		} catch {
			return undefined;
		}
	}
	if (text) {
		try {
			return JSON.parse(text) as T;
		} catch {
			return undefined;
		}
	}
	return undefined;
}

function failure(
	cls: Failure["cls"],
	summary: string,
	excerpt?: string,
): Failure {
	const firstLine = summary.split("\n", 1)[0] ?? "";
	return { cls, summary: firstLine.slice(0, 500), excerpt };
}

function resultText(result: AgentResult): string {
	if (result.text) return result.text;
	if (result.structured !== undefined) return JSON.stringify(result.structured);
	return result.error ?? result.status;
}

function resultFailure(result: AgentResult): Failure {
	if (result.status === "timeout") return failure("TIMEOUT", "agent timed out");
	if (result.status === "permission") {
		return failure(
			"PERMISSION_REQUIRED",
			result.permission?.description ?? "agent permission required",
		);
	}
	return failure(
		"AGENT_ERROR",
		result.error ?? "agent returned an error",
		result.text,
	);
}

function syntheticPlannerTask(
	run: RunState,
	config: HarnessConfig,
	attempt: number,
): TaskState {
	return {
		id: "__planner__",
		title: "Plan",
		description: run.objective,
		dependsOn: [],
		acceptance: ["Produce a valid task plan"],
		files: [],
		needsBuild: false,
		maxAttempts: config.limits.defaultMaxAttempts,
		budgetMs: config.limits.defaultBudgetMs,
		status: "RUNNING",
		attempt,
	};
}

class LeaseLostError extends Error {
	constructor() {
		super("run lease was lost");
	}
}

function errorMessage(error: unknown): string {
	return error instanceof Error ? error.message : String(error);
}

export class Runner {
	private readonly store: Store;
	private readonly agents: AgentPort;
	private readonly gates: Gates;
	private readonly context: ContextBuilder;
	private readonly config: HarnessConfig;
	private readonly repoRoot: string;
	private readonly owner: string;
	private activeRunId?: string;
	private activeAbort?: AbortController;
	private repoWriterHeld = false;
	private repoWriterTtlMs = 120_000;

	constructor(deps: {
		store: Store;
		agents: AgentPort;
		gates: Gates;
		context: ContextBuilder;
		config: HarnessConfig;
		repoRoot: string;
		owner: string;
		now?: () => Date;
	}) {
		this.store = deps.store;
		this.agents = deps.agents;
		this.gates = deps.gates;
		this.context = deps.context;
		this.config = deps.config;
		this.repoRoot = deps.repoRoot;
		this.owner = deps.owner;
	}

	async start(
		objective: string,
		opts?: { tasks?: TaskSpec[] },
	): Promise<RunState> {
		const run = this.store.createRun(objective);
		if (opts?.tasks) {
			const plan = validateTasks(opts.tasks, this.config);
			if (plan.error || !plan.tasks) {
				run.stage = "FAILED";
				this.checkpoint(run, [], {
					kind: "error",
					failure: failure(
						"CONTRACT_INVALID",
						plan.error ?? "invalid task list",
					),
				});
				return run;
			}
			this.store.upsertTasks(run.id, plan.tasks);
			this.store.appendEvent({
				runId: run.id,
				stage: run.stage,
				kind: "provided_tasks",
				data: { count: plan.tasks.length },
			});
		}
		return this.execute(run);
	}

	async resume(runId: string, opts?: { approve?: boolean }): Promise<RunState> {
		const run = this.store.getRun(runId);
		if (!run) throw new Error(`Run not found: ${runId}`);
		if (TERMINAL_STAGES[run.stage]) return run;
		if (run.stopRequested) {
			this.stop(run);
			return run;
		}
		const stopAgent =
			run.stage === "BLOCKED"
				? run.parkedReason?.match(/(?:^|;)\s*stopAgent=([^;]+)/)?.[1]
				: undefined;
		let timeoutTask: TaskState | undefined;
		if (stopAgent) {
			try {
				await this.agents.stop(stopAgent);
			} catch {
				return run;
			}
			timeoutTask = this.currentTask(run);
			if (timeoutTask?.status === "RUNNING") {
				timeoutTask.status = "PENDING";
				timeoutTask.lastFailure = {
					cls: "TIMEOUT",
					summary: "agent timed out",
				};
			}
		}
		if (run.stage === "HUMAN_APPROVAL") {
			if (!opts?.approve) return run;
			run.stage = this.resumeStage(run.parkedReason) ?? "EXECUTE";
			run.parkedReason = undefined;
			this.checkpoint(run, [], { kind: "resume", approved: true });
		} else if (run.stage === "BLOCKED") {
			run.stage = this.resumeStage(run.parkedReason) ?? "PREFLIGHT";
			run.parkedReason = undefined;
			this.checkpoint(run, timeoutTask ? [timeoutTask] : [], {
				kind: "resume",
			});
		}
		return this.execute(run);
	}

	requestStop(runId: string): void {
		const run = this.store.getRun(runId);
		if (!run || TERMINAL_STAGES[run.stage]) return;
		run.stopRequested = true;
		const lease = this.store.getRunLease(runId);
		if (!lease) {
			run.stage = "STOPPED";
			run.parkedReason = undefined;
			this.saveRun(run, []);
			this.store.appendEvent({
				runId,
				stage: "STOPPED",
				taskId: run.currentTaskId,
				kind: "transition",
				data: { outcome: "STOPPED" },
			});
			return;
		}
		this.saveRun(run, []);
		this.store.appendEvent({
			runId,
			stage: run.stage,
			taskId: run.currentTaskId,
			kind: "stop_requested",
			data: {},
		});
		if (this.activeRunId === runId) this.activeAbort?.abort();
		const task = run.currentTaskId
			? this.store.getTasks(runId).find((item) => item.id === run.currentTaskId)
			: undefined;
		for (const agentId of [task?.workerAgentId, task?.reviewerAgentId]) {
			if (agentId) void this.agents.stop(agentId).catch(() => undefined);
		}
	}

	private async execute(run: RunState): Promise<RunState> {
		const ttlMs =
			Math.max(
				60_000,
				this.config.limits.defaultBudgetMs,
				this.config.limits.buildTimeoutMs,
				this.config.limits.testTimeoutMs,
				this.config.limits.preflightTimeoutMs,
			) * 2;
		this.repoWriterTtlMs = ttlMs;
		if (!this.store.acquireRunLease(run.id, this.owner, ttlMs)) {
			throw new Error(`Run is already leased: ${run.id}`);
		}
		const abort = new AbortController();
		this.activeRunId = run.id;
		this.activeAbort = abort;
		try {
			return await this.loop(run, abort);
		} finally {
			if (this.repoWriterHeld) {
				this.store.releaseRunLease("repo-writer", this.owner);
				this.repoWriterHeld = false;
			}
			this.store.releaseRunLease(run.id, this.owner);
			if (this.activeRunId === run.id) {
				this.activeRunId = undefined;
				this.activeAbort = undefined;
			}
		}
	}

	private async loop(run: RunState, abort: AbortController): Promise<RunState> {
		for (;;) {
			const latest = this.store.getRun(run.id);
			if (!latest) throw new Error(`Run disappeared: ${run.id}`);
			run.version = latest.version;
			run.stopRequested = latest.stopRequested;
			if (run.stopRequested || abort.signal.aborted) {
				this.stop(run);
				return run;
			}
			if (TERMINAL_STAGES[run.stage] || PARKED_STAGES[run.stage]) return run;
			if (this.isRepoWriterStage(run.stage)) {
				if (
					!this.repoWriterHeld &&
					!this.store.acquireRunLease(
						"repo-writer",
						this.owner,
						this.repoWriterTtlMs,
					)
				) {
					const holder = this.store.getRunLease("repo-writer");
					const blockedStage = run.stage;
					run.stage = "BLOCKED";
					run.parkedReason = `resumeStage=${blockedStage}; repo-writer held by ${holder?.owner ?? "unknown"}`;
					this.checkpoint(run, [], {
						kind: "blocked",
						holder: holder?.owner,
					});
					return run;
				}
				this.repoWriterHeld = true;
			} else if (this.repoWriterHeld) {
				this.store.releaseRunLease("repo-writer", this.owner);
				this.repoWriterHeld = false;
			}
			try {
				await this.step(run, abort.signal);
			} catch (error) {
				if (error instanceof LeaseLostError)
					return this.store.getRun(run.id) ?? run;
				const current = this.store.getRun(run.id);
				if (abort.signal.aborted || current?.stopRequested) {
					run.stopRequested = true;
					this.stop(run);
					return run;
				}
				throw error;
			}
		}
	}

	private async step(run: RunState, signal: AbortSignal): Promise<void> {
		const from = run.stage;
		switch (from) {
			case "INTAKE":
				run.stage = transition(from, "SUCCESS", this.contextFor(run));
				this.checkpoint(
					run,
					[],
					{ kind: "transition", from, outcome: "SUCCESS" },
					from,
				);
				return;
			case "PREFLIGHT": {
				this.checkpoint(run, [], { kind: "stage_start", stage: from }, from);
				const result = await this.awaitWithLeases(run, () =>
					this.gates.preflight(signal),
				);
				if (result.outcome === "SUCCESS") {
					run.stage = this.hasProvidedTasks(run.id)
						? "TASK_CONTRACT"
						: transition(from, result.outcome, this.contextFor(run));
					this.checkpoint(
						run,
						[],
						{ kind: "gate", ...result, gate: "PREFLIGHT", next: run.stage },
						from,
					);
					return;
				}
				const gateFailure =
					result.failure ?? failure("UNKNOWN", "preflight failed");
				if (gateFailure.cls === "TOOLCHAIN_MISSING") {
					run.stage = "BLOCKED";
					run.parkedReason = this.parkedReason(from, gateFailure);
				} else {
					run.stage = transition(from, result.outcome, {
						...this.contextFor(run),
						failure: gateFailure,
					});
				}
				this.checkpoint(
					run,
					[],
					{
						kind: "gate",
						...result,
						gate: "PREFLIGHT",
						failure: gateFailure,
						next: run.stage,
					},
					from,
				);
				return;
			}
			case "PLAN":
				await this.plan(run, signal);
				return;
			case "TASK_CONTRACT":
				this.taskContract(run);
				return;
			case "EXECUTE":
				await this.executeTask(run, signal);
				return;
			case "BUILD":
				await this.runGate(run, "BUILD", signal);
				return;
			case "TEST":
				await this.runGate(run, "TEST", signal);
				return;
			case "INDEPENDENT_REVIEW":
				await this.review(run, signal);
				return;
			case "CHECKPOINT": {
				const task = this.currentTask(run);
				if (!task || task.status !== "PASSED") {
					run.stage = "FAILED";
					this.checkpoint(
						run,
						task ? [task] : [],
						{
							kind: "error",
							failure: failure("UNKNOWN", "checkpoint without a passed task"),
						},
						from,
					);
					return;
				}
				run.stage = transition(from, "SUCCESS", this.contextFor(run));
				this.checkpoint(
					run,
					[],
					{ kind: "transition", from, outcome: "SUCCESS" },
					from,
				);
				return;
			}
			case "NEXT_TASK": {
				run.currentTaskId = undefined;
				const hasMoreTasks = this.store
					.getTasks(run.id)
					.some(
						(task) => task.status === "PENDING" || task.status === "RUNNING",
					);
				run.stage = transition(from, "SUCCESS", {
					...this.contextFor(run),
					hasMoreTasks,
				});
				this.checkpoint(
					run,
					[],
					{ kind: "transition", from, outcome: "SUCCESS", hasMoreTasks },
					from,
				);
				return;
			}
		}
	}

	private async plan(run: RunState, signal: AbortSignal): Promise<void> {
		const from = run.stage;
		const attempt = this.plannerAttempt(run.id);
		const plannerTask = syntheticPlannerTask(run, this.config, attempt);
		this.checkpoint(
			run,
			[],
			{ kind: "stage_start", stage: from, attempt },
			from,
		);
		this.store.appendEvent({
			runId: run.id,
			stage: from,
			taskId: plannerTask.id,
			kind: "planner_start",
			data: { attempt },
		});
		const prompt = await this.context.forPlanner(run);
		const key = `${run.id}:${plannerTask.id}:planner:${attempt}`;
		const result = await this.awaitWithLeases(run, () =>
			this.agents.run(
				{
					role: "planner",
					idempotencyKey: key,
					title: `Plan: ${run.objective}`,
					prompt,
					cwd: this.repoRoot,
					outputSchema: PLAN_SCHEMA,
					timeoutMs: this.config.limits.defaultBudgetMs,
					labels: {
						"harness.key": key,
						"harness.runId": run.id,
						"harness.role": "planner",
					},
				},
				signal,
			),
		);
		const handoffPath = this.writeHandoff(run, plannerTask, "planner", result);
		if (result.status === "permission") {
			const permissionFailure = resultFailure(result);
			run.stage = "HUMAN_APPROVAL";
			run.parkedReason = `${this.parkedReason(from, permissionFailure)}; attempt=${attempt}`;
			this.checkpoint(
				run,
				[],
				{
					kind: "agent",
					role: "planner",
					status: result.status,
					agentId: result.agentId,
					handoffPath,
				},
				from,
			);
			this.store.appendEvent({
				runId: run.id,
				stage: from,
				taskId: plannerTask.id,
				kind: "planner_result",
				data: { attempt, outcome: "approval" },
			});
			return;
		}
		if (result.status !== "idle") {
			if (
				result.status === "timeout" &&
				!(await this.stopTimedOutAgent(run, from, result.agentId))
			)
				return;
			const agentFailure = resultFailure(result);
			this.store.appendEvent({
				runId: run.id,
				stage: from,
				taskId: plannerTask.id,
				kind: "planner_result",
				data: { attempt, outcome: "failure", failure: agentFailure },
			});
			this.finishPlannerFailure(run, from, attempt, agentFailure, handoffPath);
			return;
		}
		const output = parseStructured<PlanOutput>(result.structured, result.text);
		const plan = validatePlan(output, this.config);
		if (plan.error || !plan.tasks) {
			const invalid = failure(
				"CONTRACT_INVALID",
				plan.error ?? "invalid planner output",
			);
			this.store.appendEvent({
				runId: run.id,
				stage: from,
				taskId: plannerTask.id,
				kind: "planner_result",
				data: { attempt, outcome: "failure", failure: invalid },
			});
			this.finishPlannerFailure(run, from, attempt, invalid, handoffPath);
			return;
		}
		this.store.upsertTasks(run.id, plan.tasks);
		this.store.appendEvent({
			runId: run.id,
			stage: from,
			taskId: plannerTask.id,
			kind: "planner_result",
			data: {
				attempt,
				outcome: "success",
				count: plan.tasks.length,
				handoffPath,
			},
		});
		run.stage = "TASK_CONTRACT";
		this.checkpoint(
			run,
			[],
			{
				kind: "agent",
				role: "planner",
				status: result.status,
				agentId: result.agentId,
				handoffPath,
			},
			from,
		);
	}

	private finishPlannerFailure(
		run: RunState,
		from: Stage,
		attempt: number,
		failureValue: Failure,
		handoffPath?: string,
	): void {
		run.stage = transition(
			from,
			failureValue.cls === "TIMEOUT" ? "TIMEOUT" : "FAILURE",
			{
				...this.contextFor(run),
				attempt,
				maxAttempts: this.config.limits.defaultMaxAttempts,
				failure: failureValue,
			},
		);
		this.checkpoint(
			run,
			[],
			{
				kind: "agent",
				role: "planner",
				outcome: "failure",
				failure: failureValue,
				handoffPath,
			},
			from,
		);
	}

	private taskContract(run: RunState): void {
		const from = run.stage;
		const tasks = this.store.getTasks(run.id);
		const dirty: TaskState[] = [];
		for (const task of tasks) {
			if (
				task.status === "PENDING" &&
				task.dependsOn.some((dependency) => {
					const dep = tasks.find((candidate) => candidate.id === dependency);
					return dep?.status === "FAILED" || dep?.status === "SKIPPED";
				})
			) {
				task.status = "SKIPPED";
				dirty.push(task);
				this.checkpoint(
					run,
					[task],
					{ kind: "task_skipped", taskId: task.id },
					from,
				);
				return;
			}
		}
		const next = tasks.find(
			(task) =>
				task.status === "PENDING" &&
				task.dependsOn.every((dependency) =>
					tasks.some(
						(candidate) =>
							candidate.id === dependency && candidate.status === "PASSED",
					),
				),
		);
		if (!next) {
			const pending = tasks.some((task) => task.status === "PENDING");
			if (pending) {
				run.stage = "FAILED";
				this.checkpoint(
					run,
					dirty,
					{
						kind: "error",
						failure: failure(
							"CONTRACT_INVALID",
							"no task can satisfy its dependencies",
						),
					},
					from,
				);
				return;
			}
			run.stage = "DONE";
			this.checkpoint(
				run,
				dirty,
				{ kind: "transition", from, outcome: "SUCCESS", hasMoreTasks: false },
				from,
			);
			return;
		}
		run.currentTaskId = next.id;
		run.stage = "EXECUTE";
		this.checkpoint(
			run,
			[next, ...dirty],
			{ kind: "transition", from, outcome: "SUCCESS", taskId: next.id },
			from,
		);
	}

	private async executeTask(run: RunState, signal: AbortSignal): Promise<void> {
		const from = run.stage;
		const task = this.currentTask(run);
		if (!task) {
			run.stage = "FAILED";
			this.checkpoint(
				run,
				[],
				{ kind: "error", failure: failure("UNKNOWN", "no current task") },
				from,
			);
			return;
		}
		if (task.status === "PENDING") {
			task.status = "RUNNING";
			task.attempt += 1;
			task.workerAgentId = undefined;
			task.reviewerAgentId = undefined;
		}
		this.checkpoint(
			run,
			[task],
			{ kind: "stage_start", stage: from, attempt: task.attempt },
			from,
		);
		const key = `${run.id}:${task.id}:implementer:${task.attempt}`;
		const prompt = await this.context.forTask(run, task, "implementer");
		const result = await this.awaitWithLeases(run, () =>
			this.agents.run(
				{
					role: "implementer",
					idempotencyKey: key,
					title: `Implement ${task.id}: ${task.title}`,
					prompt,
					cwd: this.repoRoot,
					timeoutMs: task.budgetMs,
					labels: {
						"harness.key": key,
						"harness.runId": run.id,
						"harness.taskId": task.id,
						"harness.role": "implementer",
					},
				},
				signal,
				{
					onCreated: (agentId: string) => {
						task.workerAgentId = agentId;
						this.saveRun(run, [task]);
					},
				},
			),
		);
		task.workerAgentId = result.agentId;
		if (
			result.status === "timeout" &&
			!(await this.stopTimedOutAgent(run, from, result.agentId))
		)
			return;
		const handoffPath = this.writeHandoff(run, task, "implementer", result);
		if (result.status === "permission") {
			const permissionFailure = resultFailure(result);
			task.lastFailure = permissionFailure;
			run.stage = "HUMAN_APPROVAL";
			run.parkedReason = this.parkedReason(from, permissionFailure);
			this.checkpoint(
				run,
				[task],
				{
					kind: "agent",
					role: "implementer",
					status: result.status,
					agentId: result.agentId,
					handoffPath,
				},
				from,
			);
			return;
		}
		if (result.status !== "idle") {
			this.failTask(
				run,
				task,
				from,
				resultFailure(result),
				result.status === "timeout" ? "TIMEOUT" : "FAILURE",
				handoffPath,
			);
			return;
		}
		const next = task.needsBuild ? "BUILD" : "INDEPENDENT_REVIEW";
		run.stage = next;
		this.checkpoint(
			run,
			[task],
			{
				kind: "agent",
				role: "implementer",
				status: result.status,
				agentId: result.agentId,
				handoffPath,
				next,
			},
			from,
		);
	}

	private async runGate(
		run: RunState,
		gate: "BUILD" | "TEST",
		signal: AbortSignal,
	): Promise<void> {
		const from = run.stage;
		const task = this.currentTask(run);
		if (!task) {
			run.stage = "FAILED";
			this.checkpoint(
				run,
				[],
				{
					kind: "error",
					failure: failure("UNKNOWN", `no current task for ${gate}`),
				},
				from,
			);
			return;
		}
		this.checkpoint(
			run,
			[task],
			{ kind: "stage_start", stage: from, attempt: task.attempt },
			from,
		);
		const result = await this.awaitWithLeases(run, () =>
			gate === "BUILD" ? this.gates.build(signal) : this.gates.test(signal),
		);
		if (result.outcome === "SUCCESS") {
			run.stage = gate === "BUILD" ? "TEST" : "INDEPENDENT_REVIEW";
			this.checkpoint(
				run,
				[task],
				{
					kind: "gate",
					gate,
					outcome: result.outcome,
					attempt: task.attempt,
					logPath: result.logPath,
					next: run.stage,
				},
				from,
			);
			return;
		}
		const gateFailure =
			result.failure ??
			failure(
				result.outcome === "TIMEOUT" ? "TIMEOUT" : "UNKNOWN",
				`${gate} failed`,
			);
		if (gateFailure.cls === "TOOLCHAIN_MISSING") {
			task.lastFailure = gateFailure;
			run.stage = "BLOCKED";
			run.parkedReason = this.parkedReason(from, gateFailure);
			this.checkpoint(
				run,
				[task],
				{ kind: "gate", ...result, gate, failure: gateFailure },
				from,
			);
			return;
		}
		if (
			gateFailure.cls === "BUILD_LOCKED" &&
			task.attempt >= task.maxAttempts
		) {
			task.lastFailure = gateFailure;
			task.status = "BLOCKED";
			run.stage = "BLOCKED";
			run.parkedReason = this.parkedReason(from, gateFailure);
			this.checkpoint(
				run,
				[task],
				{ kind: "gate", ...result, gate, failure: gateFailure },
				from,
			);
			return;
		}
		this.failTask(
			run,
			task,
			from,
			gateFailure,
			result.outcome === "TIMEOUT" ? "TIMEOUT" : "FAILURE",
		);
	}

	private async review(run: RunState, signal: AbortSignal): Promise<void> {
		const from = run.stage;
		const task = this.currentTask(run);
		if (!task) {
			run.stage = "FAILED";
			this.checkpoint(
				run,
				[],
				{
					kind: "error",
					failure: failure("UNKNOWN", "no current task for review"),
				},
				from,
			);
			return;
		}
		this.checkpoint(
			run,
			[task],
			{ kind: "stage_start", stage: from, attempt: task.attempt },
			from,
		);
		const key = `${run.id}:${task.id}:reviewer:${task.attempt}`;
		const prompt = await this.context.forTask(run, task, "reviewer");
		const result = await this.awaitWithLeases(run, () =>
			this.agents.run(
				{
					role: "reviewer",
					idempotencyKey: key,
					title: `Review ${task.id}: ${task.title}`,
					prompt,
					cwd: this.repoRoot,
					outputSchema: REVIEW_SCHEMA,
					timeoutMs: task.budgetMs,
					labels: {
						"harness.key": key,
						"harness.runId": run.id,
						"harness.taskId": task.id,
						"harness.role": "reviewer",
					},
				},
				signal,
				{
					onCreated: (agentId: string) => {
						task.reviewerAgentId = agentId;
						this.saveRun(run, [task]);
					},
				},
			),
		);
		task.reviewerAgentId = result.agentId;
		if (
			result.status === "timeout" &&
			!(await this.stopTimedOutAgent(run, from, result.agentId))
		)
			return;
		const handoffPath = this.writeHandoff(run, task, "reviewer", result);
		if (result.status === "permission") {
			const permissionFailure = resultFailure(result);
			task.lastFailure = permissionFailure;
			run.stage = "HUMAN_APPROVAL";
			run.parkedReason = this.parkedReason(from, permissionFailure);
			this.checkpoint(
				run,
				[task],
				{
					kind: "agent",
					role: "reviewer",
					status: result.status,
					agentId: result.agentId,
					handoffPath,
				},
				from,
			);
			return;
		}
		if (result.status !== "idle") {
			this.failTask(
				run,
				task,
				from,
				resultFailure(result),
				result.status === "timeout" ? "TIMEOUT" : "FAILURE",
				handoffPath,
			);
			return;
		}
		const verdict = parseStructured<ReviewVerdict>(
			result.structured,
			result.text,
		);
		const verdictError = this.validateReview(verdict);
		if (verdictError || !verdict) {
			const reviewError = verdictError ?? "reviewer output is missing";
			this.failTask(
				run,
				task,
				from,
				failure("AGENT_ERROR", reviewError),
				"FAILURE",
				handoffPath,
			);
			return;
		}
		const blockers = verdict.findings.filter(
			(finding) => finding.severity === "blocker",
		);
		if (verdict.verdict !== "approve" || blockers.length > 0) {
			const details = verdict.findings.map(
				(finding) => `${finding.id} [${finding.severity}]: ${finding.detail}`,
			);
			const rejected = failure(
				"REVIEW_REJECTED",
				"independent review rejected the task",
				details.slice(0, 60).join("\n"),
			);
			this.failTask(run, task, from, rejected, "FAILURE", handoffPath);
			return;
		}
		if (
			task.needsBuild &&
			(!this.gateSucceeded(run.id, task.id, "BUILD", task.attempt) ||
				!this.gateSucceeded(run.id, task.id, "TEST", task.attempt))
		) {
			this.failTask(
				run,
				task,
				from,
				failure(
					"AGENT_ERROR",
					"review approval lacked successful deterministic gates",
				),
				"FAILURE",
				handoffPath,
			);
			return;
		}
		task.status = "PASSED";
		task.lastFailure = undefined;
		run.stage = "CHECKPOINT";
		this.checkpoint(
			run,
			[task],
			{
				kind: "agent",
				role: "reviewer",
				status: result.status,
				agentId: result.agentId,
				handoffPath,
				verdict: "approve",
			},
			from,
		);
	}

	private async stopTimedOutAgent(
		run: RunState,
		from: Stage,
		agentId: string,
	): Promise<boolean> {
		try {
			await this.agents.stop(agentId);
			return true;
		} catch (error) {
			run.stage = "BLOCKED";
			run.parkedReason = `resumeStage=${from}; stopAgent=${agentId}; stop failed: ${errorMessage(error)}`;
			this.checkpoint(run, [], {
				kind: "blocked",
				agentId,
				error: errorMessage(error),
			});
			return false;
		}
	}

	private isRepoWriterStage(stage: Stage): boolean {
		return (
			stage === "EXECUTE" ||
			stage === "BUILD" ||
			stage === "TEST" ||
			stage === "INDEPENDENT_REVIEW" ||
			stage === "CHECKPOINT"
		);
	}

	private failTask(
		run: RunState,
		task: TaskState,
		from: Stage,
		failureValue: Failure,
		outcome: "FAILURE" | "TIMEOUT",
		handoffPath?: string,
	): void {
		task.lastFailure = failureValue;
		const next = transition(from, outcome, {
			...this.contextFor(run),
			attempt: task.attempt,
			maxAttempts: task.maxAttempts,
			failure: failureValue,
		});
		if (next === "EXECUTE") {
			task.status = "PENDING";
			run.stage = next;
		} else {
			task.status = "FAILED";
			run.stage = next;
		}
		this.checkpoint(
			run,
			[task],
			{ kind: "failure", failure: failureValue, next: run.stage, handoffPath },
			from,
		);
	}

	private currentTask(run: RunState): TaskState | undefined {
		if (!run.currentTaskId) return undefined;
		return this.store
			.getTasks(run.id)
			.find((task) => task.id === run.currentTaskId);
	}

	private contextFor(run: RunState) {
		return {
			attempt: 0,
			maxAttempts: this.config.limits.defaultMaxAttempts,
			hasMoreTasks: this.hasMoreTasks(run.id),
		};
	}

	private hasMoreTasks(runId: string): boolean {
		return this.store
			.getTasks(runId)
			.some((task) => task.status === "PENDING" || task.status === "RUNNING");
	}

	private hasProvidedTasks(runId: string): boolean {
		return this.store
			.events(runId)
			.some((event) => event.kind === "provided_tasks");
	}

	private plannerAttempt(runId: string): number {
		const starts = this.store
			.events(runId)
			.filter((event) => event.kind === "planner_start")
			.map((event) => Number(event.data.attempt))
			.filter((attempt) => Number.isInteger(attempt) && attempt > 0);
		const last = starts.at(-1) ?? 1;
		const events = this.store.events(runId);
		const lastStart = [...events]
			.reverse()
			.find((event) => event.kind === "planner_start");
		if (!lastStart) return 1;
		const after = events.filter(
			(event) => event.seq > lastStart.seq && event.kind === "planner_result",
		);
		const result = after.at(-1);
		if (!result || result.data.outcome === "approval") return last;
		return result.data.outcome === "failure" ? last + 1 : last;
	}

	private gateSucceeded(
		runId: string,
		taskId: string,
		gate: string,
		attempt: number,
	): boolean {
		return this.store
			.events(runId)
			.some(
				(event) =>
					event.taskId === taskId &&
					event.kind === "gate" &&
					event.data.gate === gate &&
					event.data.outcome === "SUCCESS" &&
					Number(event.data.attempt) === attempt,
			);
	}

	private validateReview(
		verdict: ReviewVerdict | undefined,
	): string | undefined {
		if (
			!verdict ||
			(verdict.verdict !== "approve" && verdict.verdict !== "reject")
		) {
			return "reviewer output must contain verdict approve or reject";
		}
		if (!Array.isArray(verdict.findings))
			return "reviewer output findings must be an array";
		for (const finding of verdict.findings) {
			if (
				!record(finding) ||
				typeof finding.id !== "string" ||
				typeof finding.detail !== "string" ||
				!(["blocker", "major", "minor"] as const).includes(
					finding.severity as never,
				)
			) {
				return "reviewer finding is invalid";
			}
		}
		return undefined;
	}

	private writeHandoff(
		run: RunState,
		task: TaskState,
		role: Role,
		result: AgentResult,
	): string | undefined {
		try {
			return this.context.writeHandoff(run, task, {
				role,
				summary: resultText(result),
				failure: result.status === "idle" ? undefined : resultFailure(result),
				agentId: result.agentId,
			});
		} catch {
			return undefined;
		}
	}

	private renewLeases(runId: string): boolean {
		if (!this.store.renewRunLease(runId, this.owner, this.repoWriterTtlMs))
			return false;
		return (
			!this.repoWriterHeld ||
			this.store.renewRunLease("repo-writer", this.owner, this.repoWriterTtlMs)
		);
	}

	private async awaitWithLeases<T>(
		run: RunState,
		operation: () => Promise<T>,
	): Promise<T> {
		let lost = false;
		const timer = setInterval(
			() => {
				try {
					if (!this.renewLeases(run.id)) {
						lost = true;
						this.activeAbort?.abort();
					}
				} catch {
					lost = true;
					this.activeAbort?.abort();
				}
			},
			Math.max(1, Math.floor(this.repoWriterTtlMs / 3)),
		);
		try {
			try {
				const result = await operation();
				if (lost) throw new LeaseLostError();
				return result;
			} catch (error) {
				if (lost) throw new LeaseLostError();
				throw error;
			}
		} finally {
			clearInterval(timer);
		}
	}

	private checkpoint(
		run: RunState,
		dirtyTasks: TaskState[],
		data: Record<string, unknown>,
		eventStage?: Stage,
	): void {
		if (this.activeRunId === run.id && !this.renewLeases(run.id)) {
			this.activeAbort?.abort();
			throw new LeaseLostError();
		}
		this.saveRun(run, dirtyTasks);
		this.store.appendEvent({
			runId: run.id,
			stage: eventStage ?? run.stage,
			taskId: run.currentTaskId,
			kind: typeof data.kind === "string" ? data.kind : "checkpoint",
			data,
		});
	}

	private saveRun(run: RunState, dirtyTasks: TaskState[]): void {
		for (let retry = 0; retry < 2; retry += 1) {
			for (const task of dirtyTasks) this.store.saveTask(run.id, task);
			try {
				const saved = this.store.saveRun(run);
				Object.assign(run, saved);
				return;
			} catch (error) {
				const latest = this.store.getRun(run.id);
				if (!latest || retry === 1) throw error;
				run.version = latest.version;
				run.stopRequested ||= latest.stopRequested;
			}
		}
	}

	private stop(run: RunState): void {
		run.stage = "STOPPED";
		run.parkedReason = undefined;
		this.checkpoint(run, [], { kind: "transition", outcome: "STOPPED" });
	}

	private parkedReason(stage: Stage, reason: Failure): string {
		return `resumeStage=${stage}; ${reason.cls}: ${reason.summary}`;
	}

	private resumeStage(reason: string | undefined): Stage | undefined {
		const match = reason?.match(/(?:^|;)\s*resumeStage=([A-Z_]+)/);
		return match?.[1] as Stage | undefined;
	}
}
