import { afterEach, describe, expect, it } from "bun:test";
import { mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { Store } from "../src/store.ts";
import type { TaskState } from "../src/types.ts";

const openStores: Store[] = [];
const tempDirs: string[] = [];

afterEach(() => {
	for (const store of openStores.splice(0)) store.close();
	for (const dir of tempDirs.splice(0))
		rmSync(dir, { recursive: true, force: true });
});

function databasePath() {
	const dir = mkdtempSync(join(tmpdir(), "psl-harness-store-"));
	tempDirs.push(dir);
	return join(dir, "state.db");
}

function task(id = "task-1"): TaskState {
	return {
		id,
		title: "Task",
		description: "A task",
		dependsOn: [],
		acceptance: ["It works"],
		files: ["Source/**"],
		needsBuild: false,
		maxAttempts: 2,
		budgetMs: 1000,
		status: "PENDING",
		attempt: 0,
	};
}

describe("PSL.Store", () => {
	it("persists runs, tasks, events, and optimistic versions", () => {
		const path = databasePath();
		const first = new Store(path);
		openStores.push(first);
		const created = first.createRun("ship the slice", "run-1");
		first.upsertTasks(created.id, [task()]);
		const saved = first.saveRun({ ...created, stage: "PREFLIGHT" });
		expect(saved.version).toBe(1);
		first.appendEvent({
			runId: created.id,
			stage: "PREFLIGHT",
			kind: "transition",
			data: { ok: true },
		});
		first.appendEvent({
			runId: created.id,
			stage: "PLAN",
			kind: "transition",
			data: {},
		});
		first.close();
		openStores.splice(openStores.indexOf(first), 1);

		const second = new Store(path);
		openStores.push(second);
		expect(second.getRun(created.id)).toMatchObject({
			stage: "PREFLIGHT",
			version: 1,
		});
		expect(second.getTasks(created.id)).toEqual([task()]);
		expect(second.events(created.id).map((event) => event.seq)).toEqual([1, 2]);
		expect(() => second.saveRun(created)).toThrow(/version/i);
	});

	it("acquires, renews, and releases an exclusive run lease", () => {
		const store = new Store(databasePath());
		openStores.push(store);
		const run = store.createRun("lease", "lease-run");
		expect(store.acquireRunLease(run.id, "owner-a", 60_000)).toBe(true);
		expect(store.acquireRunLease(run.id, "owner-b", 60_000)).toBe(false);
		expect(store.getRunLease(run.id)?.owner).toBe("owner-a");
		store.releaseRunLease(run.id, "owner-b");
		expect(store.getRunLease(run.id)?.owner).toBe("owner-a");
		store.releaseRunLease(run.id, "owner-a");
		expect(store.getRunLease(run.id)).toBeNull();
	});
});
