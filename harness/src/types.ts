// Shared contract for the harness. Every module imports from here; change only with all owners.
export const APPROVAL_OPERATIONS = [
	"git.commit",
	"git.push",
	"git.merge",
	"privileged",
	"destructive",
	"repo.visibility",
] as const;
export type ApprovalOperation = (typeof APPROVAL_OPERATIONS)[number];
/** Only the controlling-terminal challenge mints grants; CLI flags and forms never do. */
export type ApprovalSource = "tty-challenge";
/** Parked request; `agent.permission` is an OMP/Paseo tool permission the worker asked for. */
export interface ApprovalRequest {
	operation: ApprovalOperation | "agent.permission";
	target?: string;
}
export interface ApprovalGrant {
	operation: ApprovalOperation | "agent.permission";
	approved: true;
	source: ApprovalSource;
	runId: string;
	runVersion: number;
	nonce: string;
	target?: string;
}

export interface ApprovalDecision {
	approved: boolean;
	reason?: string;
}


export const STAGES = [
	"INTAKE",
	"PREFLIGHT",
	"PLAN",
	"TASK_CONTRACT",
	"EXECUTE",
	"BUILD",
	"TEST",
	"INDEPENDENT_REVIEW",
	"CHECKPOINT",
	"NEXT_TASK",
	"DONE",
	"BLOCKED",
	"FAILED",
	"HUMAN_APPROVAL",
	"STOPPED",
] as const;
export type Stage = (typeof STAGES)[number];

/** Stages from which the runner never advances on its own. */
export const TERMINAL_STAGES: Partial<Record<Stage, true>> = {
	DONE: true,
	FAILED: true,
	STOPPED: true,
};
/** Stages that pause until a human or `resume` acts. */
export const PARKED_STAGES: Partial<Record<Stage, true>> = {
	BLOCKED: true,
	HUMAN_APPROVAL: true,
};

/** Outcome of one stage handler. Deterministic gates produce these; LLM text never does directly. */
export type Outcome =
	| "SUCCESS"
	| "FAILURE"
	| "BLOCKED"
	| "TIMEOUT"
	| "NEEDS_APPROVAL";

export type FailureClass =
	| "TOOLCHAIN_MISSING" // UE/Xcode absent or misconfigured -> BLOCKED
	| "DISK_LOW" // free space below the preflight floor -> BLOCKED; never auto-clean caches
	| "BUILD_LOCKED" // another full UE build holds the lock -> retry later
	| "COMPILE_ERROR"
	| "LINK_ERROR"
	| "TEST_FAILURE"
	| "TIMEOUT"
	| "PERMISSION_REQUIRED"
	| "AGENT_ERROR" // worker crashed / returned invalid structured output
	| "CONTRACT_INVALID" // plan/task contract failed validation
	| "REVIEW_REJECTED"
	| "REVIEW_PROTOCOL_ERROR" // reviewer reply not in a documented format; never an implementer fault
	| "CANCELLED"
	| "UNKNOWN";

/** Environment failures park the run as BLOCKED instead of consuming retries. */
export const BLOCKING_FAILURES: ReadonlySet<FailureClass> = new Set([
	"TOOLCHAIN_MISSING",
	"DISK_LOW",
]);

export interface Failure {
	cls: FailureClass;
	summary: string; // one line
	excerpt?: string; // <= ~60 relevant lines, never a full log
	logPath?: string; // full log on disk
}

export type TaskStatus =
	| "PENDING"
	| "RUNNING"
	| "PASSED"
	| "FAILED"
	| "BLOCKED"
	| "SKIPPED";

export interface TaskSpec {
	id: string; // unique within a run, [A-Za-z0-9_-]+
	title: string;
	description: string;
	dependsOn: string[];
	acceptance: string[]; // non-empty
	files: string[]; // repo-relative paths/globs the task may touch (context hints)
	needsBuild: boolean; // run BUILD/TEST gates
	maxAttempts: number; // >= 1
	budgetMs: number; // wall-clock budget per attempt for the worker
}

export interface TaskState extends TaskSpec {
	status: TaskStatus;
	attempt: number; // attempts started, 0 before first EXECUTE
	lastFailure?: Failure;
	workerAgentId?: string; // Paseo agent of the current attempt
	reviewerAgentId?: string;
}

export interface RunState {
	id: string;
	objective: string;
	stage: Stage;
	currentTaskId?: string;
	createdAt: string;
	updatedAt: string;
	stopRequested: boolean;
	parkedReason?: string; // set when BLOCKED / HUMAN_APPROVAL
	approvalOperation?: ApprovalOperation | "agent.permission";
	approvalTarget?: string;
	worktreePath?: string;
	worktreeBranch?: string;
	version: number; // optimistic concurrency, +1 per checkpoint
}

export interface HarnessEvent {
	runId: string;
	seq: number;
	at: string;
	stage: Stage;
	taskId?: string;
	kind: string; // e.g. "transition", "gate", "agent", "error"
	data: Record<string, unknown>;
}

// ---- Verification gates (deterministic) ----

export interface GateResult {
	gate: "PREFLIGHT" | "BUILD" | "TEST";
	outcome: Outcome;
	exitCode: number | null; // null when killed by timeout
	durationMs: number;
	command: string[];
	logPath: string;
	failure?: Failure;
}

// ---- Agent port (Paseo in production, fake in tests) ----

export type Role = "planner" | "implementer" | "reviewer";

export interface AgentRequest {
	role: Role;
	/** Stable key: same key => same agent; resume never spawns a duplicate. */
	idempotencyKey: string;
	title: string;
	prompt: string;
	cwd: string;
	outputSchema?: Record<string, unknown>;
	timeoutMs: number;
	labels: Record<string, string>;
}

export interface AgentResult {
	status: "idle" | "error" | "permission" | "timeout";
	agentId: string;
	workspaceId?: string;
	structured?: unknown; // parsed JSON when outputSchema was given
	text?: string;
	error?: string;
	permission?: { requestId: string; description: string };
}

export interface AgentRunHooks {
	onCreated?(agentId: string): void;
}

export interface AgentPort {
	run(
		req: AgentRequest,
		signal?: AbortSignal,
		hooks?: AgentRunHooks,
	): Promise<AgentResult>;
	stop(agentId: string): Promise<void>;
}

/** Reviewer structured output. Approval only counts if all deterministic gates also passed. */
export interface ReviewVerdict {
	verdict: "approve" | "reject";
	findings: {
		id: string;
		severity: "blocker" | "major" | "minor";
		detail: string;
	}[];
}

/** Planner structured output. */
export interface PlanOutput {
	tasks: (Omit<TaskSpec, "maxAttempts" | "budgetMs"> &
		Partial<Pick<TaskSpec, "maxAttempts" | "budgetMs">>)[];
}

// ---- Config ----

export interface RoleConfig {
	provider: string; // "omp/<model-id>"
	modeId: string;
	thinkingOptionId?: string;
	/** Repo-relative OMP config overlay; passed to the agent as PI_CONFIG_FILES. */
	configOverlay?: string;
}

export interface HarnessConfig {
	daemonUrl: string;
	roles: Record<Role, RoleConfig>;
	limits: {
		defaultMaxAttempts: number;
		defaultBudgetMs: number;
		buildTimeoutMs: number;
		testTimeoutMs: number;
		preflightTimeoutMs: number;
		maxTasksPerRun: number;
	};
	commands: { preflight: string[]; build: string[]; test: string[] };
	useWorktrees: boolean; // requires a Git HEAD
}
