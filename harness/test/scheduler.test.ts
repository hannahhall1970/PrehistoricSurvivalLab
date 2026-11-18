import { describe, expect, it } from "bun:test";
import { selectRunnable } from "../src/scheduler.ts";
import type { TaskState, TaskStatus } from "../src/types.ts";

function task(
	id: string,
	over: Partial<TaskState> & { status?: TaskStatus } = {},
): TaskState {
	return {
		id,
		title: id,
		description: id,
		dependsOn: [],
		acceptance: [],
		files: [`src/${id}.ts`],
		needsBuild: false,
		maxAttempts: 3,
		budgetMs: 1000,
		status: "PENDING",
		attempt: 0,
		...over,
	} as TaskState;
}

describe("PSL.selectRunnable", () => {
	it("starts independent tasks up to maxParallel in plan order", () => {
		const r = selectRunnable([task("a"), task("b"), task("c"), task("d")], {
			maxParallel: 3,
		});
		expect(r.start).toEqual(["a", "b", "c"]);
		expect(r.waiting).toEqual({ d: "capacity" });
	});

	it("defaults maxParallel to 3", () => {
		const r = selectRunnable(["a", "b", "c", "d"].map((i) => task(i)));
		expect(r.start).toHaveLength(3);
	});

	it("holds a dependent until its dependency PASSED", () => {
		const before = selectRunnable([
			task("A", { status: "RUNNING" }),
			task("D", { dependsOn: ["A"] }),
		]);
		expect(before.start).toEqual([]);
		expect(before.waiting.D).toBe("deps:A");
		const after = selectRunnable([
			task("A", { status: "PASSED" }),
			task("D", { dependsOn: ["A"] }),
		]);
		expect(after.start).toEqual(["D"]);
	});

	it("reports a failed/skipped/blocked dependency as the waiting reason", () => {
		for (const status of ["FAILED", "SKIPPED", "BLOCKED"] as const) {
			const r = selectRunnable([
				task("A", { status }),
				task("D", { dependsOn: ["A"] }),
			]);
			expect(r.start).toEqual([]);
			expect(r.waiting.D).toBe(`dep-${status.toLowerCase()}:A`);
		}
	});

	it("never co-starts the same file", () => {
		const r = selectRunnable([
			task("a", { files: ["src/x.ts"] }),
			task("b", { files: ["src/x.ts"] }),
		]);
		expect(r.start).toEqual(["a"]);
		expect(r.waiting.b).toBe("file-conflict:a");
	});

	it("treats a directory prefix as overlapping, but not a sibling name", () => {
		expect(
			selectRunnable([
				task("a", { files: ["src/dir"] }),
				task("b", { files: ["src/dir/x.ts"] }),
			]).start,
		).toEqual(["a"]);
		expect(
			selectRunnable([
				task("a", { files: ["src/dir"] }),
				task("b", { files: ["src/dir2/x.ts"] }),
			]).start,
		).toEqual(["a", "b"]);
	});

	it("treats a glob as covering its static prefix", () => {
		expect(
			selectRunnable([
				task("a", { files: ["docs/**/*.md"] }),
				task("b", { files: ["docs/x/y.md"] }),
			]).start,
		).toEqual(["a"]);
		expect(
			selectRunnable([
				task("a", { files: ["docs/*.md"] }),
				task("b", { files: ["src/a.ts"] }),
			]).start,
		).toEqual(["a", "b"]);
		expect(
			selectRunnable([
				task("a", { files: ["**/*.cpp"] }),
				task("b", { files: ["src/a.ts"] }),
			]).start,
		).toEqual(["a"]);
	});

	it("does not start a task overlapping a RUNNING one", () => {
		const r = selectRunnable([
			task("run", { status: "RUNNING", files: ["src/dir"] }),
			task("b", { files: ["src/dir/x.ts"] }),
			task("c", { files: ["other/c.ts"] }),
		]);
		expect(r.start).toEqual(["c"]);
		expect(r.waiting.b).toBe("file-conflict:run");
	});

	it("runs a task with empty files alone", () => {
		const r = selectRunnable([
			task("a"),
			task("all", { files: [] }),
			task("c"),
		]);
		expect(r.start).toEqual(["a", "c"]);
		expect(r.waiting).toEqual({ all: "file-conflict:a" });
		expect(
			selectRunnable([task("all", { files: [] }), task("c")]).start,
		).toEqual(["all"]);
		expect(
			selectRunnable([task("run", { status: "RUNNING" }), task("all", { files: [] })])
				.start,
		).toEqual([]);
	});

	it("starts only one needsBuild task with default ueSlots", () => {
		const r = selectRunnable([
			task("a", { needsBuild: true }),
			task("b", { needsBuild: true }),
			task("c"),
		]);
		expect(r.start).toEqual(["a", "c"]);
		expect(r.waiting.b).toBe("ue-slot");
	});

	it("counts a RUNNING needsBuild task against ueSlots", () => {
		const r = selectRunnable([
			task("run", { status: "RUNNING", needsBuild: true }),
			task("b", { needsBuild: true }),
		]);
		expect(r.start).toEqual([]);
		expect(r.waiting.b).toBe("ue-slot");
		expect(
			selectRunnable(
				[
					task("run", { status: "RUNNING", needsBuild: true }),
					task("b", { needsBuild: true }),
				],
				{ maxParallel: 3, ueSlots: 2 },
			).start,
		).toEqual(["b"]);
	});

	it("subtracts RUNNING tasks from capacity", () => {
		const r = selectRunnable(
			[
				task("r1", { status: "RUNNING" }),
				task("r2", { status: "RUNNING" }),
				task("a"),
				task("b"),
			],
			{ maxParallel: 3 },
		);
		expect(r.start).toEqual(["a"]);
		expect(r.waiting.b).toBe("capacity");
		expect(
			selectRunnable(
				[task("r1", { status: "RUNNING" }), task("a")],
				{ maxParallel: 1 },
			).waiting.a,
		).toBe("capacity");
	});

	it("is deterministic and does not mutate input", () => {
		const tasks = [
			task("a"),
			task("b", { files: ["src/a.ts"] }),
			task("c", { needsBuild: true }),
			task("d", { dependsOn: ["a"] }),
		];
		const snapshot = JSON.stringify(tasks);
		const first = selectRunnable(tasks, { maxParallel: 2 });
		expect(selectRunnable(tasks, { maxParallel: 2 })).toEqual(first);
		expect(JSON.stringify(tasks)).toBe(snapshot);
	});
});
