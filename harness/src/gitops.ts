import {
	chmodSync,
	existsSync,
	mkdtempSync,
	readFileSync,
	rmSync,
	writeFileSync,
} from "node:fs";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";
import { type ApprovalTarget, requireApproval } from "./approval.ts";
import {
	type AutoContext,
	autoCommit,
	autoStage,
	classifyOperation,
	type Risk,
} from "./policy.ts";
import { cleanGitEnv } from "./worktree.ts";

/**
 * Trusted Git executor. These four operations are the ONLY Git writes the harness performs.
 * argv is built here from fixed templates; callers pass data (SHAs, paths, message), never flags.
 * Approval is bound to the operation id plus exact SHAs/remote, so a human approving "push main"
 * cannot be turned into a different command. Same-UID limitation: this is not an OS sandbox.
 */
export type GitOp =
	| {
			id: "stage.exact";
			worktree: string;
			paths: string[];
			expectedBlobs: Record<string, string>;
	  }
	| { id: "commit.task"; worktree: string; message: string }
	| {
			id: "merge.ff-main";
			sourceBranch: string;
			sourceSha: string;
			targetSha: string;
	  }
	| {
			id: "push.main";
			remoteUrl: string;
			sourceSha: string;
			targetSha: string;
	  };

export type GitOpResult =
	| { ok: true; sha?: string; argv: string[] }
	| { ok: false; reason: string };

export interface GitOpEvent {
	kind: "auto_op" | "protected_op";
	data: Record<string, unknown>;
}

export interface GitOpDeps {
	repoRoot: string;
	runId: string;
	runVersion: number;
	log: (event: GitOpEvent) => void;
	/** Required by stage.exact: the task's declared changed files. */
	declaredFiles?: string[];
	/** Required by commit.task: BUILD and TEST SUCCESS for the current tree. */
	gatesOk?: () => boolean;
	/** Tip of refs/heads/main on origin; injectable for tests. */
	lsRemote?: (repoRoot: string) => string | undefined;
}

const SHA = /^[0-9a-f]{40}$/;
const FIELDS: Record<string, string[]> = {
	"stage.exact": ["expectedBlobs", "id", "paths", "worktree"],
	"commit.task": ["id", "message", "worktree"],
	"merge.ff-main": ["id", "sourceBranch", "sourceSha", "targetSha"],
	"push.main": ["id", "remoteUrl", "sourceSha", "targetSha"],
};

/** ONLY for classifying worker-requested permission text; the harness never executes strings. */
export function classifyCommand(text: string): Risk {
	return classifyOperation(text);
}

function git(cwd: string, args: string[]) {
	const r = Bun.spawnSync(["git", ...args], {
		cwd,
		env: { ...cleanGitEnv(), GIT_TERMINAL_PROMPT: "0" },
		stdout: "pipe",
		stderr: "pipe",
	});
	return {
		ok: r.exitCode === 0,
		out: new TextDecoder().decode(r.stdout).trim(),
		err: new TextDecoder().decode(r.stderr).trim(),
	};
}

export function defaultLsRemote(repoRoot: string): string | undefined {
	const r = git(repoRoot, ["ls-remote", "origin", "refs/heads/main"]);
	return r.ok ? r.out.split(/\s+/)[0] || undefined : undefined;
}

/**
 * Stock pre-push hook as the installed git-lfs prints it (`install --local --manual` only prints).
 * Run in a throwaway repo so the real repo's config is never touched; no network.
 */
function stockLfsPrePush(): string | undefined {
	const dir = mkdtempSync(join(tmpdir(), "lfs-hook-"));
	try {
		if (!git(dir, ["init", "-q", "."]).ok) return undefined;
		const printed = git(dir, ["lfs", "install", "--local", "--manual"]);
		if (!printed.ok) return undefined;
		const lines = printed.out.split("\n");
		const start = lines.findIndex((l) => l.includes("hooks/pre-push"));
		if (start < 0) return undefined;
		const body: string[] = [];
		for (const line of lines.slice(start + 1)) {
			if (line.startsWith("\t")) body.push(line.slice(1));
			else if (body.length > 0) break;
		}
		return body.length > 0 ? `${body.join("\n")}\n` : undefined;
	} finally {
		rmSync(dir, { recursive: true, force: true });
	}
}

/** Fail closed unless pre-push is absent or byte-equal to the stock git-lfs hook. */
export function prePushHookProblem(repoRoot: string): string | undefined {
	const hooksPath = git(repoRoot, ["config", "--get", "core.hooksPath"]);
	if (hooksPath.ok && hooksPath.out)
		return "core.hooksPath is set; push hooks are not trusted";
	const where = git(repoRoot, ["rev-parse", "--git-path", "hooks/pre-push"]);
	if (!where.ok) return "cannot locate the pre-push hook";
	const file = resolve(repoRoot, where.out);
	if (!existsSync(file)) return undefined;
	const stock = stockLfsPrePush();
	if (stock === undefined)
		return "pre-push hook exists and the stock git-lfs hook is unavailable to compare";
	return readFileSync(file, "utf8") === stock
		? undefined
		: "pre-push hook is not the stock git-lfs hook";
}

export function executeGitOp(
	op: GitOp,
	deps: GitOpDeps,
	grant?: unknown,
): GitOpResult {
	const known = typeof op === "object" && op !== null;
	const id = known && "id" in op ? String(op.id) : "(none)";
	const kind =
		id === "stage.exact" || id === "commit.task" ? "auto_op" : "protected_op";
	const audit = (reason: string, argv: string[] | null, ok: boolean): void =>
		deps.log({
			kind,
			data: {
				opId: id,
				argv,
				outcome: ok ? "ok" : "rejected",
				reason,
				runId: deps.runId,
			},
		});
	const reject = (reason: string): GitOpResult => {
		audit(reason, null, false);
		return { ok: false, reason };
	};

	const expected = FIELDS[id];
	if (!known || !expected)
		return reject(`unknown git operation ${JSON.stringify(id)}`);
	if (Object.keys(op).sort().join() !== expected.join())
		return reject(`${id} has unexpected or missing fields`);

	if (op.id === "stage.exact" || op.id === "commit.task") {
		// policy.ts logs its own auto_op event (with the git outcome); no second event here.
		const ctx: AutoContext = {
			repoRoot: deps.repoRoot,
			runId: deps.runId,
			worktreePath: op.worktree,
			declaredFiles: deps.declaredFiles ?? [],
			gatesOk: deps.gatesOk ?? (() => false),
			log: (event) =>
				deps.log({
					kind: "auto_op",
					data: { ...event.data, opId: op.id },
				}),
		};
		if (op.id === "stage.exact") {
			const staged = autoStage(ctx, op.paths, op.expectedBlobs);
			return staged.ok
				? { ok: true, argv: ["add", "--", ...op.paths] }
				: { ok: false, reason: staged.reason };
		}
		const committed = autoCommit(ctx, op.message);
		return committed.ok
			? {
					ok: true,
					argv: ["commit", "-q", "-m", op.message],
					...(committed.sha ? { sha: committed.sha } : {}),
				}
			: { ok: false, reason: committed.reason };
	}

	// ---- ASK operations: merge.ff-main, push.main ----
	if (!SHA.test(op.sourceSha) || !SHA.test(op.targetSha))
		return reject("sha must be 40 lowercase hex characters");
	const origin = git(deps.repoRoot, ["remote", "get-url", "origin"]);
	if (!origin.ok || !origin.out) return reject("origin remote is unavailable");
	const main = git(deps.repoRoot, ["rev-parse", "--verify", "refs/heads/main"]);
	let argv: string[];
	let lfsHook: string | undefined;
	if (op.id === "merge.ff-main") {
		if (op.sourceBranch !== `harness/${deps.runId}`)
			return reject("source branch is not this run's harness branch");
		if (git(deps.repoRoot, ["branch", "--show-current"]).out !== "main")
			return reject("current branch is not main");
		if (!main.ok || main.out !== op.targetSha)
			return reject("main tip differs from targetSha");
		const tip = git(deps.repoRoot, [
			"rev-parse",
			"--verify",
			`refs/heads/${op.sourceBranch}`,
		]);
		if (!tip.ok || tip.out !== op.sourceSha)
			return reject("source branch tip differs from sourceSha");
		argv = ["merge", "--ff-only", op.sourceSha];
	} else {
		if (op.remoteUrl !== origin.out)
			return reject("remoteUrl does not equal origin");
		if (!main.ok || main.out !== op.sourceSha)
			return reject("local main differs from sourceSha");
		if ((deps.lsRemote ?? defaultLsRemote)(deps.repoRoot) !== op.targetSha)
			return reject("origin main differs from targetSha");
		// N4: followTags must not publish extra refs next to the approved one.
		argv = [
			"push",
			"--no-follow-tags",
			"origin",
			`${op.sourceSha}:refs/heads/main`,
		];
		// R5: a pushurl could send the push somewhere other than the approved remote.
		const pushUrls = git(deps.repoRoot, [
			"remote",
			"get-url",
			"--push",
			"--all",
			"origin",
		]);
		if (
			!pushUrls.ok ||
			pushUrls.out.split("\n").filter(Boolean).join() !== op.remoteUrl
		)
			return reject("origin push URL differs from the approved remoteUrl");
		// R10c: the only hook allowed to run during the push is the stock git-lfs one.
		const hookReason = prePushHookProblem(deps.repoRoot);
		if (hookReason) return reject(hookReason);
		// N3: the push runs with a hooks dir that holds only the stock pre-push; no stock text, no push.
		lfsHook = stockLfsPrePush();
		if (lfsHook === undefined)
			return reject(
				"stock git-lfs pre-push hook is unavailable; refusing push",
			);
	}
	if (
		!git(deps.repoRoot, [
			"merge-base",
			"--is-ancestor",
			op.targetSha,
			op.sourceSha,
		]).ok
	)
		return reject("targetSha is not an ancestor of sourceSha");

	const request: ApprovalTarget = {
		operation: op.id === "merge.ff-main" ? "git.merge" : "git.push",
		runId: deps.runId,
		runVersion: deps.runVersion,
		target: "main",
		sourceSha: op.sourceSha,
		targetSha: op.targetSha,
		remoteUrl: origin.out,
	};
	const decision = requireApproval(request, grant);
	if (!decision.approved) return reject(decision.reason ?? "not approved");
	// N3: no repo hook runs during merge or push. Merge disables hooks; push uses a harness-owned
	// hooks dir that holds only the stock git-lfs pre-push (reference-transaction, post-merge, ... never run).
	let hooksDir: string | undefined;
	try {
		let full: string[];
		if (lfsHook !== undefined) {
			hooksDir = mkdtempSync(join(tmpdir(), "push-hooks-"));
			writeFileSync(join(hooksDir, "pre-push"), lfsHook);
			chmodSync(join(hooksDir, "pre-push"), 0o755);
			full = [
				"-c",
				"push.followTags=false",
				"-c",
				`core.hooksPath=${hooksDir}`,
				...argv,
			];
		} else {
			full = ["-c", "core.hooksPath=/dev/null", ...argv];
		}
		const ran = git(deps.repoRoot, full);
		if (!ran.ok) return reject(ran.err || `git ${argv[0]} failed`);
		audit("approved", full, true);
		return { ok: true, argv: full };
	} finally {
		if (hooksDir) rmSync(hooksDir, { recursive: true, force: true });
	}
}
