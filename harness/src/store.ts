import { Database } from "bun:sqlite";
import { mkdirSync } from "node:fs";
import { dirname, isAbsolute } from "node:path";
import type { HarnessEvent, RunState, Stage, TaskState } from "./types.ts";

type RunRow = {
	id: string;
	objective: string;
	stage: Stage;
	current_task_id: string | null;
	created_at: string;
	updated_at: string;
	stop_requested: number;
	parked_reason: string | null;
	approval_operation: string | null;
	approval_target: string | null;
	worktree_path: string | null;
	worktree_branch: string | null;
	base_sha: string | null;
	version: number;
};

type TaskRow = {
	run_id: string;
	id: string;
	title: string;
	description: string;
	depends_on: string;
	acceptance: string;
	files: string;
	needs_build: number;
	max_attempts: number;
	budget_ms: number;
	status: TaskState["status"];
	attempt: number;
	last_failure: string | null;
	worker_agent_id: string | null;
	reviewer_agent_id: string | null;
	ord: number;
};

type EventRow = {
	run_id: string;
	seq: number;
	at: string;
	stage: Stage;
	task_id: string | null;
	kind: string;
	data: string;
};

type LeaseRow = {
	run_id: string;
	owner: string;
	acquired_at: string;
	expires_at: number;
	pid: number;
};

export interface RunLease {
	owner: string;
	at: string;
	expiresAt: number;
	pid?: number;
}

function json<T>(value: string | null, fallback: T): T {
	if (value === null) return fallback;
	try {
		return JSON.parse(value) as T;
	} catch {
		return fallback;
	}
}

function nullable(value: string | undefined): string | null {
	return value ?? null;
}

function bool(value: number): boolean {
	return value !== 0;
}
function pidAlive(pid: number): boolean {
	if (pid <= 0) return false;
	try {
		process.kill(pid, 0);
		return true;
	} catch {
		return false;
	}
}

export class Store {
	private readonly db: Database;

	constructor(dbPath = ".harness/state.db") {
		if (dbPath !== ":memory:" && !dbPath.startsWith("file:")) {
			const directory = dirname(
				isAbsolute(dbPath) ? dbPath : `${process.cwd()}/${dbPath}`,
			);
			mkdirSync(directory, { recursive: true });
		}
		this.db = new Database(dbPath);
		this.db.run("PRAGMA journal_mode = WAL");
		this.db.run("PRAGMA busy_timeout = 5000");
		this.db.run(`
			CREATE TABLE IF NOT EXISTS runs (
				id TEXT PRIMARY KEY,
				objective TEXT NOT NULL,
				stage TEXT NOT NULL,
				current_task_id TEXT,
				created_at TEXT NOT NULL,
				updated_at TEXT NOT NULL,
				stop_requested INTEGER NOT NULL DEFAULT 0,
				parked_reason TEXT,
				approval_operation TEXT,
				approval_target TEXT,
				worktree_path TEXT,
				worktree_branch TEXT,
				version INTEGER NOT NULL DEFAULT 0
			)
		`);
		this.db.run(`
			CREATE TABLE IF NOT EXISTS tasks (
				run_id TEXT NOT NULL,
				id TEXT NOT NULL,
				title TEXT NOT NULL,
				description TEXT NOT NULL,
				depends_on TEXT NOT NULL,
				acceptance TEXT NOT NULL,
				files TEXT NOT NULL,
				needs_build INTEGER NOT NULL,
				max_attempts INTEGER NOT NULL,
				budget_ms INTEGER NOT NULL,
				status TEXT NOT NULL,
				attempt INTEGER NOT NULL DEFAULT 0,
				last_failure TEXT,
				worker_agent_id TEXT,
				reviewer_agent_id TEXT,
				ord INTEGER NOT NULL DEFAULT 0,
				PRIMARY KEY (run_id, id)
			)
		`);
		this.db.run(`
			CREATE TABLE IF NOT EXISTS events (
				run_id TEXT NOT NULL,
				seq INTEGER NOT NULL,
				at TEXT NOT NULL,
				stage TEXT NOT NULL,
				task_id TEXT,
				kind TEXT NOT NULL,
				data TEXT NOT NULL,
				PRIMARY KEY (run_id, seq)
			)
		`);
		this.db.run(`
			CREATE TABLE IF NOT EXISTS run_leases (
				run_id TEXT PRIMARY KEY,
				owner TEXT NOT NULL,
				acquired_at TEXT NOT NULL,
				expires_at INTEGER NOT NULL,
				pid INTEGER NOT NULL DEFAULT 0
			)
		`);
		try {
			this.db.run(
				"ALTER TABLE run_leases ADD COLUMN pid INTEGER NOT NULL DEFAULT 0",
			);
		} catch {
			// Existing databases already have the column.
		}
		for (const column of [
			"approval_operation TEXT",
			"approval_target TEXT",
			"worktree_path TEXT",
			"worktree_branch TEXT",
			"base_sha TEXT",
		]) {
			try {
				this.db.run(`ALTER TABLE runs ADD COLUMN ${column}`);
			} catch {
				// Existing databases already have the column.
			}
		}
	}

	createRun(objective: string, id?: string, baseSha?: string): RunState {
		const now = new Date().toISOString();
		const runId = id ?? crypto.randomUUID();
		const run: RunState = {
			id: runId,
			objective,
			stage: "INTAKE",
			createdAt: now,
			updatedAt: now,
			stopRequested: false,
			version: 0,
			...(baseSha ? { baseSha } : {}),
		};
		this.db.run(
			`INSERT INTO runs
				(id, objective, stage, current_task_id, created_at, updated_at, stop_requested,
				 parked_reason, approval_operation, approval_target, worktree_path, worktree_branch, base_sha, version)
			 VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)`,
			[
				run.id,
				run.objective,
				run.stage,
				null,
				run.createdAt,
				run.updatedAt,
				0,
				null,
				null,
				null,
				null,
				null,
				nullable(run.baseSha),
				run.version,
			],
		);
		return run;
	}

	getRun(id: string): RunState | undefined {
		const row = this.db
			.query<RunRow, [string]>("SELECT * FROM runs WHERE id = ?")
			.get(id);
		return row ? this.toRun(row) : undefined;
	}

	listRuns(): RunState[] {
		return this.db
			.query<RunRow, []>("SELECT * FROM runs ORDER BY updated_at DESC, id DESC")
			.all()
			.map((row) => this.toRun(row));
	}

	saveRun(run: RunState): RunState {
		let updatedAt = new Date().toISOString();
		if (updatedAt <= run.updatedAt) {
			updatedAt = new Date(Date.parse(run.updatedAt) + 1).toISOString();
		}
		const nextVersion = run.version + 1;
		const result = this.db.run(
			`UPDATE runs SET
				objective = ?, stage = ?, current_task_id = ?, updated_at = ?,
				stop_requested = ?, parked_reason = ?, approval_operation = ?,
				approval_target = ?, worktree_path = ?, worktree_branch = ?, version = ?
			 WHERE id = ? AND version = ?`,
			[
				run.objective,
				run.stage,
				nullable(run.currentTaskId),
				updatedAt,
				run.stopRequested ? 1 : 0,
				nullable(run.parkedReason),
				nullable(run.approvalOperation),
				nullable(run.approvalTarget),
				nullable(run.worktreePath),
				nullable(run.worktreeBranch),
				nextVersion,
				run.id,
				run.version,
			],
		);
		if (result.changes !== 1)
			throw new Error(`Run version mismatch: ${run.id}`);
		const saved = this.getRun(run.id);
		if (!saved) throw new Error(`Run disappeared while saving: ${run.id}`);
		return saved;
	}

	upsertTasks(runId: string, tasks: TaskState[]): void {
		const upsert = this.db.transaction((items: TaskState[]) => {
			for (const [index, task] of items.entries()) {
				this.db.run(
					`INSERT INTO tasks
						(run_id, id, title, description, depends_on, acceptance, files, needs_build,
						 max_attempts, budget_ms, status, attempt, last_failure, worker_agent_id,
						 reviewer_agent_id, ord)
					 VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
					 ON CONFLICT(run_id, id) DO UPDATE SET
						title = excluded.title, description = excluded.description,
						depends_on = excluded.depends_on, acceptance = excluded.acceptance,
						files = excluded.files, needs_build = excluded.needs_build,
						max_attempts = excluded.max_attempts, budget_ms = excluded.budget_ms,
						status = excluded.status, attempt = excluded.attempt,
						last_failure = excluded.last_failure,
						worker_agent_id = excluded.worker_agent_id,
						reviewer_agent_id = excluded.reviewer_agent_id,
						ord = CASE WHEN tasks.ord = 0 AND excluded.ord <> 0 THEN excluded.ord ELSE tasks.ord END`,
					[
						runId,
						task.id,
						task.title,
						task.description,
						JSON.stringify(task.dependsOn),
						JSON.stringify(task.acceptance),
						JSON.stringify(task.files),
						task.needsBuild ? 1 : 0,
						task.maxAttempts,
						task.budgetMs,
						task.status,
						task.attempt,
						task.lastFailure ? JSON.stringify(task.lastFailure) : null,
						nullable(task.workerAgentId),
						nullable(task.reviewerAgentId),
						index,
					],
				);
			}
		})(tasks);
		void upsert;
	}

	getTasks(runId: string): TaskState[] {
		return this.db
			.query<TaskRow, [string]>(
				"SELECT * FROM tasks WHERE run_id = ? ORDER BY ord ASC, id ASC",
			)
			.all(runId)
			.map((row) => this.toTask(row));
	}

	saveTask(runId: string, task: TaskState): void {
		const existing = this.db
			.query<{ ord: number }, [string, string]>(
				"SELECT ord FROM tasks WHERE run_id = ? AND id = ?",
			)
			.get(runId, task.id);
		const ord = existing?.ord ?? this.nextTaskOrder(runId);
		this.db.run(
			`INSERT INTO tasks
				(run_id, id, title, description, depends_on, acceptance, files, needs_build,
				 max_attempts, budget_ms, status, attempt, last_failure, worker_agent_id,
				 reviewer_agent_id, ord)
			 VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
			 ON CONFLICT(run_id, id) DO UPDATE SET
				title = excluded.title, description = excluded.description,
				depends_on = excluded.depends_on, acceptance = excluded.acceptance,
				files = excluded.files, needs_build = excluded.needs_build,
				max_attempts = excluded.max_attempts, budget_ms = excluded.budget_ms,
				status = excluded.status, attempt = excluded.attempt,
				last_failure = excluded.last_failure,
				worker_agent_id = excluded.worker_agent_id,
				reviewer_agent_id = excluded.reviewer_agent_id`,
			[
				runId,
				task.id,
				task.title,
				task.description,
				JSON.stringify(task.dependsOn),
				JSON.stringify(task.acceptance),
				JSON.stringify(task.files),
				task.needsBuild ? 1 : 0,
				task.maxAttempts,
				task.budgetMs,
				task.status,
				task.attempt,
				task.lastFailure ? JSON.stringify(task.lastFailure) : null,
				nullable(task.workerAgentId),
				nullable(task.reviewerAgentId),
				ord,
			],
		);
	}

	appendEvent(event: Omit<HarnessEvent, "seq" | "at">): HarnessEvent {
		const append = this.db.transaction(
			(input: Omit<HarnessEvent, "seq" | "at">) => {
				const next = this.db
					.query<{ seq: number }, [string]>(
						"SELECT COALESCE(MAX(seq), 0) + 1 AS seq FROM events WHERE run_id = ?",
					)
					.get(input.runId);
				const seq = next?.seq ?? 1;
				const at = new Date().toISOString();
				this.db.run(
					`INSERT INTO events (run_id, seq, at, stage, task_id, kind, data)
				 VALUES (?, ?, ?, ?, ?, ?, ?)`,
					[
						input.runId,
						seq,
						at,
						input.stage,
						nullable(input.taskId),
						input.kind,
						JSON.stringify(input.data),
					],
				);
				return { ...input, seq, at };
			},
		)(event);
		return append;
	}

	events(runId: string, sinceSeq?: number): HarnessEvent[] {
		const rows =
			sinceSeq === undefined
				? this.db
						.query<EventRow, [string]>(
							"SELECT * FROM events WHERE run_id = ? ORDER BY seq ASC",
						)
						.all(runId)
				: this.db
						.query<EventRow, [string, number]>(
							"SELECT * FROM events WHERE run_id = ? AND seq > ? ORDER BY seq ASC",
						)
						.all(runId, sinceSeq);
		return rows.map((row) => ({
			runId: row.run_id,
			seq: row.seq,
			at: row.at,
			stage: row.stage,
			taskId: row.task_id ?? undefined,
			kind: row.kind,
			data: json<Record<string, unknown>>(row.data, {}),
		}));
	}

	acquireRunLease(runId: string, owner: string, ttlMs: number): boolean {
		const now = Date.now();
		const acquiredAt = new Date(now).toISOString();
		const acquireTransaction = this.db.transaction(
			(id: string, leaseOwner: string, ttl: number) => {
				const current = this.db
					.query<LeaseRow, [string]>(
						"SELECT * FROM run_leases WHERE run_id = ?",
					)
					.get(id);
				if (current) {
					const expired = current.expires_at <= now;
					if (current.owner !== leaseOwner && !expired) return false;
					if (expired) {
						const removed = this.db.run(
							"DELETE FROM run_leases WHERE run_id = ? AND owner = ? AND expires_at <= ?",
							[id, current.owner, current.expires_at],
						);
						if (removed.changes !== 1) return false;
					} else {
						this.db.run(
							`UPDATE run_leases SET acquired_at = ?, expires_at = ?, pid = ?
							 WHERE run_id = ? AND owner = ?`,
							[acquiredAt, now + Math.max(1, ttl), process.pid, id, leaseOwner],
						);
						return true;
					}
				}
				this.db.run(
					`INSERT INTO run_leases (run_id, owner, acquired_at, expires_at, pid)
					 VALUES (?, ?, ?, ?, ?)`,
					[id, leaseOwner, acquiredAt, now + Math.max(1, ttl), process.pid],
				);
				return true;
			},
		);
		return acquireTransaction.immediate(runId, owner, ttlMs);
	}

	renewRunLease(runId: string, owner: string, ttlMs: number): boolean {
		const now = Date.now();
		const result = this.db.run(
			"UPDATE run_leases SET expires_at = ? WHERE run_id = ? AND owner = ? AND expires_at > ?",
			[now + Math.max(1, ttlMs), runId, owner, now],
		);
		return result.changes === 1;
	}

	releaseRunLease(runId: string, owner: string): void {
		this.db.run("DELETE FROM run_leases WHERE run_id = ? AND owner = ?", [
			runId,
			owner,
		]);
	}

	getRunLease(runId: string): RunLease | null {
		const row = this.db
			.query<LeaseRow, [string]>("SELECT * FROM run_leases WHERE run_id = ?")
			.get(runId);
		if (!row) return null;
		const stale = row.expires_at <= Date.now() || !pidAlive(row.pid);
		if (stale) {
			this.db.run(
				"DELETE FROM run_leases WHERE run_id = ? AND owner = ? AND expires_at <= ?",
				[runId, row.owner, row.expires_at],
			);
			return null;
		}
		return {
			owner: row.owner,
			at: row.acquired_at,
			expiresAt: row.expires_at,
			pid: row.pid,
		};
	}

	close(): void {
		this.db.close();
	}

	private nextTaskOrder(runId: string): number {
		const row = this.db
			.query<{ ord: number }, [string]>(
				"SELECT COALESCE(MAX(ord), -1) + 1 AS ord FROM tasks WHERE run_id = ?",
			)
			.get(runId);
		return row?.ord ?? 0;
	}

	private toRun(row: RunRow): RunState {
		return {
			id: row.id,
			objective: row.objective,
			stage: row.stage,
			currentTaskId: row.current_task_id ?? undefined,
			createdAt: row.created_at,
			updatedAt: row.updated_at,
			stopRequested: bool(row.stop_requested),
			parkedReason: row.parked_reason ?? undefined,
			approvalOperation:
				(row.approval_operation as RunState["approvalOperation"]) ?? undefined,
			approvalTarget: row.approval_target ?? undefined,
			worktreePath: row.worktree_path ?? undefined,
			worktreeBranch: row.worktree_branch ?? undefined,
			baseSha: row.base_sha ?? undefined,
			version: row.version,
		};
	}

	private toTask(row: TaskRow): TaskState {
		return {
			id: row.id,
			title: row.title,
			description: row.description,
			dependsOn: json<string[]>(row.depends_on, []),
			acceptance: json<string[]>(row.acceptance, []),
			files: json<string[]>(row.files, []),
			needsBuild: bool(row.needs_build),
			maxAttempts: row.max_attempts,
			budgetMs: row.budget_ms,
			status: row.status,
			attempt: row.attempt,
			lastFailure: json(row.last_failure, undefined),
			workerAgentId: row.worker_agent_id ?? undefined,
			reviewerAgentId: row.reviewer_agent_id ?? undefined,
		};
	}
}
