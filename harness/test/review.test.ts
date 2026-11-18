import { describe, expect, it } from "bun:test";
import { normalizeReview } from "../src/review.ts";

const finding = { id: "F1", severity: "minor" as const, detail: "d" };

describe("normalizeReview", () => {
	it("accepts the top-level format", () => {
		expect(normalizeReview({ verdict: "approve", findings: [] })).toEqual({
			ok: true,
			verdict: { verdict: "approve", findings: [] },
		});
	});

	it("accepts the named envelope and normalizes to the same shape", () => {
		const nested = normalizeReview({
			ReviewVerdict: { verdict: "approve", findings: [finding], task: "x" },
		});
		expect(nested).toEqual({
			ok: true,
			verdict: { verdict: "approve", findings: [finding] },
		});
	});

	it("parses JSON from a string or from text when structured is absent", () => {
		const json = JSON.stringify({
			ReviewVerdict: {
				verdict: "reject",
				findings: [{ ...finding, severity: "blocker" }],
			},
		});
		const fromString = normalizeReview(json);
		const fromText = normalizeReview(undefined, json);
		expect(fromString.ok && fromString.verdict.verdict).toBe("reject");
		expect(fromText).toEqual(fromString);
	});

	it("keeps reject semantics and blocker severity", () => {
		const parsed = normalizeReview({
			verdict: "reject",
			findings: [{ ...finding, severity: "blocker" }],
		});
		expect(parsed.ok && parsed.verdict.findings[0]?.severity).toBe("blocker");
	});

	it("rejects conflicting top-level and envelope verdicts", () => {
		const parsed = normalizeReview({
			verdict: "reject",
			findings: [],
			ReviewVerdict: { verdict: "approve", findings: [] },
		});
		expect(parsed.ok).toBe(false);
	});

	it("rejects an envelope with extra top-level keys", () => {
		expect(
			normalizeReview({
				ReviewVerdict: { verdict: "approve", findings: [] },
				other: 1,
			}).ok,
		).toBe(false);
	});

	it.each([
		["missing verdict", { findings: [] }],
		["unknown verdict", { verdict: "approved", findings: [] }],
		["missing findings", { verdict: "approve" }],
		["findings not array", { verdict: "approve", findings: {} }],
		[
			"finding bad severity",
			{ verdict: "approve", findings: [{ ...finding, severity: "info" }] },
		],
		[
			"finding missing id",
			{ verdict: "approve", findings: [{ severity: "minor", detail: "d" }] },
		],
		["envelope not object", { ReviewVerdict: "approve" }],
		["array", [{ verdict: "approve", findings: [] }]],
	])("rejects %s", (_name, value) => {
		expect(normalizeReview(value).ok).toBe(false);
	});

	it("rejects malformed JSON and never scans prose for 'approve'", () => {
		expect(normalizeReview(undefined, "{not json").ok).toBe(false);
		expect(normalizeReview("{not json").ok).toBe(false);
		expect(normalizeReview(undefined, "I approve this change.").ok).toBe(false);
		expect(normalizeReview(undefined, undefined).ok).toBe(false);
	});
});
