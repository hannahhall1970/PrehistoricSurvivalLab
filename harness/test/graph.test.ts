import { describe, expect, it } from "bun:test";
import { EDGE_TABLE, requiresBuild, transition } from "../src/graph.ts";
import type { Outcome, Stage } from "../src/types.ts";

const context = { attempt: 1, maxAttempts: 3, hasMoreTasks: true };
describe("PSL.requiresBuild", () => {
	it("requires verification for build-relevant changed files", () => {
		expect(
			requiresBuild(
				["docs/x.md"],
				["Source/PrehistoricSurvivalLab/Private/A.cpp"],
				false,
			).required,
		).toBe(true);
	});

	it("allows docs-only declared and changed files", () => {
		const result = requiresBuild(["docs/**"], ["docs/x.md"], false);
		expect(result.required).toBe(false);
		expect(result.reason).toContain("checked");
	});

	it("fails closed when changed files are unavailable", () => {
		expect(requiresBuild(["docs/**"], null, false).required).toBe(true);
	});

	it("never removes planner-requested verification", () => {
		expect(requiresBuild(["docs/**"], ["docs/x.md"], true).required).toBe(true);
	});

	it("recognizes Unreal project, config, and build metadata paths", () => {
		for (const path of [
			"Foo.Build.cs",
			"X.uproject",
			"Config/DefaultGame.ini",
		]) {
			expect(requiresBuild(["docs/**"], [path], false).required).toBe(true);
		}
	});
});

const expected: [Stage, Outcome, Stage][] = [
	["INTAKE", "SUCCESS", "PREFLIGHT"],
	["PREFLIGHT", "SUCCESS", "PLAN"],
	["PLAN", "SUCCESS", "TASK_CONTRACT"],
	["TASK_CONTRACT", "SUCCESS", "EXECUTE"],
	["EXECUTE", "SUCCESS", "BUILD"],
	["BUILD", "SUCCESS", "TEST"],
	["TEST", "SUCCESS", "INDEPENDENT_REVIEW"],
	["INDEPENDENT_REVIEW", "SUCCESS", "CHECKPOINT"],
	["CHECKPOINT", "SUCCESS", "NEXT_TASK"],
	["NEXT_TASK", "SUCCESS", "TASK_CONTRACT"],
];

describe("PSL.transition", () => {
	it("follows every successful edge", () => {
		for (const [stage, outcome, next] of expected) {
			expect(EDGE_TABLE[stage][outcome]).toBe(next);
			expect(transition(stage, outcome, context)).toBe(next);
		}
		expect(
			transition("NEXT_TASK", "SUCCESS", { ...context, hasMoreTasks: false }),
		).toBe("DONE");
	});

	it("retries worker stages and then fails at the bound", () => {
		for (const stage of [
			"EXECUTE",
			"BUILD",
			"TEST",
			"INDEPENDENT_REVIEW",
		] as const) {
			expect(transition(stage, "FAILURE", context)).toBe("EXECUTE");
			expect(
				transition(stage, "TIMEOUT", {
					...context,
					attempt: context.maxAttempts,
				}),
			).toBe("FAILED");
		}
		expect(transition("PLAN", "FAILURE", context)).toBe("PLAN");
		expect(
			transition("PLAN", "FAILURE", {
				...context,
				attempt: context.maxAttempts,
			}),
		).toBe("FAILED");
	});

	it("parks approvals and blocked failures", () => {
		expect(transition("EXECUTE", "NEEDS_APPROVAL", context)).toBe(
			"HUMAN_APPROVAL",
		);
		expect(transition("PREFLIGHT", "BLOCKED", context)).toBe("BLOCKED");
		expect(
			transition("PREFLIGHT", "FAILURE", {
				...context,
				failure: { cls: "TOOLCHAIN_MISSING", summary: "missing UE" },
			}),
		).toBe("BLOCKED");
	});
});
