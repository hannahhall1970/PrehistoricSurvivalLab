import { isRecord } from "./config.ts";
import type { ReviewVerdict } from "./types.ts";

/** Exact reviewer reply format; keep in sync with normalizeReview(). */
export const REVIEW_FORMAT =
	'{"verdict":"approve","findings":[{"id":"F1","severity":"minor","detail":"..."}]}';

/** Reviewer-only retries per review() call before the run parks as BLOCKED. */
export const MAX_REVIEW_PROTOCOL_TRIES = 3;

export type ReviewParse =
	| { ok: true; verdict: ReviewVerdict }
	| { ok: false; error: string };

const SEVERITIES = ["blocker", "major", "minor"] as const;

function load(structured: unknown, text?: string): unknown {
	if (structured !== undefined && structured !== null) {
		if (typeof structured !== "string") return structured;
		try {
			return JSON.parse(structured);
		} catch {
			return undefined;
		}
	}
	if (!text) return undefined;
	try {
		return JSON.parse(text);
	} catch {
		return undefined;
	}
}

/**
 * Accepts exactly two shapes and normalizes both to ReviewVerdict:
 *   A. {"verdict":"approve|reject","findings":[...]}
 *   B. {"ReviewVerdict":{"verdict":"approve|reject","findings":[...]}}
 * Never searches free text for the word "approve".
 */
export function normalizeReview(
	structured: unknown,
	text?: string,
): ReviewParse {
	const raw = load(structured, text);
	if (raw === undefined)
		return { ok: false, error: "reviewer output is missing or not valid JSON" };
	if (!isRecord(raw))
		return { ok: false, error: "reviewer output must be a JSON object" };

	let body: Record<string, unknown>;
	if ("ReviewVerdict" in raw) {
		if ("verdict" in raw || "findings" in raw)
			return {
				ok: false,
				error:
					"conflicting review representations: ReviewVerdict envelope plus top-level verdict/findings",
			};
		if (Object.keys(raw).length !== 1)
			return {
				ok: false,
				error: "ReviewVerdict envelope must be the only top-level key",
			};
		if (!isRecord(raw.ReviewVerdict))
			return {
				ok: false,
				error: "ReviewVerdict envelope must contain an object",
			};
		body = raw.ReviewVerdict;
	} else {
		body = raw;
	}

	if (body.verdict !== "approve" && body.verdict !== "reject")
		return {
			ok: false,
			error: "reviewer output must contain verdict approve or reject",
		};
	if (!Array.isArray(body.findings))
		return { ok: false, error: "reviewer output findings must be an array" };

	const findings: ReviewVerdict["findings"] = [];
	for (const finding of body.findings) {
		if (
			!isRecord(finding) ||
			typeof finding.id !== "string" ||
			finding.id === "" ||
			typeof finding.detail !== "string" ||
			!SEVERITIES.includes(finding.severity as never)
		)
			return {
				ok: false,
				error:
					"reviewer finding is invalid: each needs string id, severity blocker|major|minor, string detail",
			};
		findings.push({
			id: finding.id,
			severity:
				finding.severity as ReviewVerdict["findings"][number]["severity"],
			detail: finding.detail,
		});
	}
	return { ok: true, verdict: { verdict: body.verdict, findings } };
}
