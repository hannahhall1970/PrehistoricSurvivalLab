import { describe, expect, it } from "bun:test";
import { EDGE_TABLE, transition } from "../src/graph.ts";
import type { Outcome, Stage } from "../src/types.ts";

const context = { attempt: 1, maxAttempts: 3, hasMoreTasks: true };

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
