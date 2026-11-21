import { closeSync, openSync, readSync, writeSync } from "node:fs";
import { randomBytes } from "node:crypto";
import {
	APPROVAL_OPERATIONS,
	type ApprovalDecision,
	type ApprovalGrant,
	type ApprovalOperation,
	type ApprovalRequest,
} from "./types.ts";

/**
 * Trust model (separation of duties):
 * - Worker request: a run parks in HUMAN_APPROVAL with `approvalOperation` (+ optional target).
 * - Trusted approval: only `TtyApprovalSource` mints grants, and only after a human types a
 *   one-time code shown on the controlling terminal. Agent shells (Paseo/OMP bash/eval) have
 *   no controlling terminal, so `/dev/tty` fails and the source refuses (fail closed).
 * - Grant validation: `requireApproval` accepts only grants minted in this process, bound to
 *   run id + run version + operation + target, and consumes them (single use).
 * - Execution: the runner leaves HUMAN_APPROVAL only after validation succeeds.
 * CLI flags (`--approve-op`, `--approve`) are requests for a challenge, never grants.
 */

const operations = new Set<string>(APPROVAL_OPERATIONS);
const minted = new WeakSet<object>();

export type ApprovalTarget = ApprovalRequest & {
	runId: string;
	runVersion: number;
};

/** Human channel. The real one is the controlling terminal; tests inject a scripted one. */
export interface ApprovalIO {
	ask(prompt: string): string | undefined;
}

export const controllingTerminal: ApprovalIO = {
	ask(prompt) {
		let fd: number;
		try {
			fd = openSync("/dev/tty", "r+");
		} catch {
			return undefined; // no controlling terminal: agent or CI context
		}
		try {
			writeSync(fd, prompt);
			const buffer = Buffer.alloc(256);
			let line = "";
			while (!line.includes("\n")) {
				const count = readSync(fd, buffer, 0, buffer.length, null);
				if (count <= 0) break;
				line += buffer.subarray(0, count).toString("utf8");
			}
			return line.split("\n")[0]?.trim();
		} finally {
			closeSync(fd);
		}
	},
};

export class TtyApprovalSource {
	constructor(private readonly io: ApprovalIO = controllingTerminal) {}

	/** Show a one-time code for exactly this request; mint a grant only if it is typed back. */
	confirm(request: ApprovalTarget): ApprovalGrant | undefined {
		if (
			!operations.has(request.operation) &&
			request.operation !== "agent.permission"
		)
			return undefined;
		const nonce = randomBytes(3).toString("hex").toUpperCase();
		const answer = this.io.ask(
			`\nHARNESS APPROVAL REQUIRED\n  run: ${request.runId}\n  operation: ${request.operation}\n  target: ${request.target ?? "(none)"}\nType ${nonce} to approve this one operation (anything else denies): `,
		);
		if (answer !== nonce) return undefined;
		const grant: ApprovalGrant = Object.freeze({
			operation: request.operation,
			approved: true,
			source: "tty-challenge",
			runId: request.runId,
			runVersion: request.runVersion,
			nonce,
			...(request.sourceSha !== undefined
				? { sourceSha: request.sourceSha }
				: {}),
			...(request.targetSha !== undefined
				? { targetSha: request.targetSha }
				: {}),
			...(request.remoteUrl !== undefined
				? { remoteUrl: request.remoteUrl }
				: {}),
			...(request.target !== undefined ? { target: request.target } : {}),
		});
		minted.add(grant);
		return grant;
	}
}

export interface ReleaseTransaction {
	runId: string;
	runVersion: number;
	sourceBranch: string;
	targetBranch: string;
	sourceSha: string;
	targetSha: string;
	remoteUrl: string;
	files: string[];
	/** Git LFS objects the push would publish (read-only `git lfs ls-files`), shown to the human. */
	lfsObjects: string[];
	operations: Array<"git.merge" | "git.push">;
}

/**
 * One human confirmation for a whole release transaction. Mints one distinct single-use grant per
 * operation, each bound to {operation, runId, runVersion, target, sourceSha, targetSha, remoteUrl}.
 */
export function confirmTransaction(
	tx: ReleaseTransaction,
	io: ApprovalIO = controllingTerminal,
): Map<ApprovalOperation, ApprovalGrant> | undefined {
	const nonce = randomBytes(3).toString("hex").toUpperCase();
	const answer = io.ask(
		`\nHARNESS RELEASE TRANSACTION\n  run: ${tx.runId}\n  source: ${tx.sourceBranch} @ ${tx.sourceSha}\n  target: ${tx.targetBranch} @ ${tx.targetSha}\n  remote: ${tx.remoteUrl}\n  files: ${tx.files.join(", ") || "(none)"}\n  lfs objects published: ${tx.lfsObjects.join(", ") || "(none)"}\n  operations: ${tx.operations.join(", ")}\nType ${nonce} to approve exactly these operations (anything else denies): `,
	);
	if (answer !== nonce) return undefined;
	const grants = new Map<ApprovalOperation, ApprovalGrant>();
	for (const operation of tx.operations) {
		const grant: ApprovalGrant = Object.freeze({
			operation,
			approved: true,
			source: "tty-challenge",
			runId: tx.runId,
			runVersion: tx.runVersion,
			nonce,
			target: tx.targetBranch,
			sourceSha: tx.sourceSha,
			targetSha: tx.targetSha,
			remoteUrl: tx.remoteUrl,
		});
		minted.add(grant);
		grants.set(operation, grant);
	}
	return grants;
}

function blocked(operation: string, reason: string): ApprovalDecision {
	return {
		approved: false,
		reason: `Approval blocked for ${operation}: ${reason}`,
	};
}

/**
 * Validate a grant for one parked request. Object literals, JSON, CLI flags, free text and
 * replayed grants are rejected: only an unused grant minted by `TtyApprovalSource` passes.
 */
export function requireApproval(
	request: ApprovalTarget,
	grant: unknown,
): ApprovalDecision {
	const operation = String(request?.operation);
	if (!operations.has(operation) && operation !== "agent.permission")
		return blocked(operation, "unknown operation");
	if (typeof grant !== "object" || grant === null)
		return blocked(operation, "missing user approval");
	if (!minted.has(grant))
		return blocked(
			operation,
			"grant was not issued by the trusted approval source",
		);
	const g = grant as ApprovalGrant;
	minted.delete(grant); // single use, even when the checks below fail
	if (g.operation !== operation)
		return blocked(operation, "grant operation does not match");
	if (g.runId !== request.runId || g.runVersion !== request.runVersion)
		return blocked(operation, "grant is for a different run or run state");
	if ((g.target ?? null) !== (request.target ?? null))
		return blocked(operation, "grant target does not match");
	for (const key of ["sourceSha", "targetSha", "remoteUrl"] as const)
		if ((g[key] ?? null) !== (request[key] ?? null))
			return blocked(operation, `grant ${key} does not match`);
	return { approved: true };
}

export type {
	ApprovalDecision,
	ApprovalGrant,
	ApprovalOperation,
	ApprovalRequest,
};
