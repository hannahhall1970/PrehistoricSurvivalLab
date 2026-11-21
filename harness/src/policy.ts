import { lstatSync } from "node:fs";
import { resolve } from "node:path";
import { type ApprovalTarget, requireApproval } from "./approval.ts";
import { cleanGitEnv, validateWorktree } from "./worktree.ts";

/**
 * Risk-based policy. The harness (never the worker) performs AUTO operations; ASK operations
 * need a trusted grant from `approval.ts`; DENY operations cannot be unlocked by any grant.
 * Unknown operations are ASK (fail closed). This is a command allowlist, not an OS sandbox.
 */
export type Risk = "AUTO" | "ASK" | "DENY";

const DENY: RegExp[] = [
	/\bpush\b.*(--force|--mirror|--delete|--prune|\s-[a-zA-Z]*[fd][a-zA-Z]*\b|\s\+\S|\s:\S)/, // incl. --force-with-lease, -uf/-fu, -d, :ref
	/\breset\b.*--hard/,
	/\b(rebase|filter-branch|filter-repo)\b|--amend\b/,
	/\bgit\s+clean\b|\bclean\s+-\S*[fx]/,
	/\blfs\s+push\b/, // publishes objects outside the approved ref update
	/--output\b/, // writes an arbitrary file
];

const AUTO: RegExp[] = [
	/^(inspect|search|read|grep|edit|preflight|build|test|evidence|tmp)\b/,
	/^git\s+(status|diff|log|show|rev-parse|ls-files|hash-object)\b/,
	/^recovery\.(copy|move-verified-duplicate)\b/,
];

/** Repo-relative exact path: no `..`, no leading `-`, no glob, not the whole tree. */
export function isExactRepoPath(path: unknown): path is string {
	return (
		typeof path === "string" &&
		path !== "" &&
		path !== "." &&
		!path.startsWith("/") &&
		!path.startsWith("-") &&
		!/[*?[\]\0\n\\:]/.test(path) &&
		!path
			.split("/")
			.some((part) => part === ".." || part === "." || part === "")
	);
}

export function classifyOperation(
	op: string,
	ctx?: { runId?: string; branch?: string },
): Risk {
	const text = op.trim().toLowerCase();
	if (DENY.some((re) => re.test(text))) return "DENY";
	// Shell metacharacters can chain a second command behind an allowed one: never AUTO.
	if (/[;&|$`()<>\n\r]/.test(op)) return "ASK";
	if (/^git\s+add\b/.test(text)) {
		const paths = op
			.trim()
			.match(/^git\s+add\s+--\s+(.+)$/i)?.[1]
			?.split(/\s+/);
		return paths?.every(isExactRepoPath) ? "AUTO" : "DENY"; // -A, ., globs, bare add
	}
	if (/^git\s+commit\b/.test(text))
		return ctx?.runId && ctx.branch === `harness/${ctx.runId}` ? "AUTO" : "ASK";
	return AUTO.some((re) => re.test(text)) ? "AUTO" : "ASK";
}

/** Gate for any operation: DENY never passes, ASK needs a trusted grant, AUTO passes. */
export function authorize(
	op: string,
	request: ApprovalTarget,
	grant: unknown,
	ctx?: { runId?: string; branch?: string },
): { approved: boolean; reason?: string } {
	const risk = classifyOperation(op, ctx);
	if (risk === "DENY")
		return { approved: false, reason: `DENY: ${op} cannot be approved` };
	if (risk === "AUTO") return { approved: true };
	return requireApproval(request, grant);
}

export type PolicyResult =
	| { ok: true; sha?: string }
	| { ok: false; reason: string };

export interface AutoContext {
	repoRoot: string;
	runId: string;
	worktreePath: string;
	/** Task's declared changed files (exact repo-relative paths). */
	declaredFiles: string[];
	/** Audit sink; production passes `store.appendEvent` with kind "auto_op". */
	log: (event: {
		kind: "auto_op";
		data: {
			op: string;
			reason: string;
			paths: string[];
			runId: string;
			outcome: "ok" | "rejected";
		};
	}) => void;
	/** BUILD and TEST gates SUCCESS for the current treeFingerprint. */
	gatesOk: () => boolean;
}

const SECRET_SCAN = resolve(import.meta.dir, "../../scripts/check_secrets.sh");

function git(cwd: string, args: string[]) {
	const r = Bun.spawnSync(["git", ...args], {
		cwd,
		env: cleanGitEnv(),
		stdout: "pipe",
		stderr: "pipe",
	});
	return {
		exitCode: r.exitCode,
		stdout: new TextDecoder().decode(r.stdout),
		stderr: new TextDecoder().decode(r.stderr),
	};
}

function finish(
	ctx: AutoContext,
	op: string,
	reason: string,
	paths: string[],
	result: PolicyResult,
): PolicyResult {
	ctx.log({
		kind: "auto_op",
		data: {
			op,
			reason,
			paths,
			runId: ctx.runId,
			outcome: result.ok ? "ok" : "rejected",
			...(result.ok && result.sha ? { sha: result.sha } : {}),
		},
	});
	return result;
}

const reject = (reason: string): PolicyResult => ({ ok: false, reason });

export function autoStage(
	ctx: AutoContext,
	paths: string[],
	expectedBlobs: Record<string, string>,
): PolicyResult {
	const op = "git add --";
	const done = (reason: string, result: PolicyResult) =>
		finish(ctx, op, reason, paths, result);
	const valid = validateWorktree(
		ctx.repoRoot,
		ctx.runId,
		ctx.worktreePath,
		`harness/${ctx.runId}`,
	);
	if (!valid.ok) return done(valid.reason, reject(valid.reason));
	if (paths.length === 0) return done("no paths", reject("no paths to stage"));
	for (const path of paths) {
		if (!isExactRepoPath(path))
			return done("unsafe path", reject(`unsafe path ${JSON.stringify(path)}`));
		if (!ctx.declaredFiles.includes(path))
			return done(
				"undeclared path",
				reject(`${path} is not a declared changed file`),
			);
		try {
			if (lstatSync(resolve(valid.path, path)).isSymbolicLink())
				return done("symlink", reject(`${path} is a symlink`));
		} catch {
			return done("missing path", reject(`${path} does not exist`));
		}
		const blob = git(valid.path, ["hash-object", "--", path]);
		if (blob.exitCode !== 0 || blob.stdout.trim() !== expectedBlobs[path])
			return done(
				"blob mismatch",
				reject(`${path} does not match its expected blob`),
			);
	}
	const add = git(valid.path, ["add", "--", ...paths]);
	if (add.exitCode !== 0)
		return done(
			"git add failed",
			reject(add.stderr.trim() || "git add failed"),
		);
	// Close the hash/add TOCTOU: the blob actually staged must be the one the gates covered.
	for (const path of paths) {
		const indexed = git(valid.path, ["rev-parse", `:${path}`]);
		if (
			indexed.exitCode !== 0 ||
			indexed.stdout.trim() !== expectedBlobs[path]
		) {
			git(valid.path, ["restore", "--staged", "--", ...paths]);
			return done(
				"staged blob mismatch",
				reject(`${path} changed between hashing and staging`),
			);
		}
	}
	// --no-renames: a rename must list both its old and new path, so a hidden deletion shows up.
	const staged = git(valid.path, [
		"diff",
		"--cached",
		"--no-renames",
		"--name-only",
		"-z",
	])
		.stdout.split("\0")
		.filter(Boolean)
		.sort();
	const wanted = [...new Set(paths)].sort();
	if (
		staged.length !== wanted.length ||
		staged.some((p, i) => p !== wanted[i])
	) {
		git(valid.path, ["restore", "--staged", "--", ...paths]);
		return done(
			"staged set differs",
			reject("staged set differs from requested paths"),
		);
	}
	return done("exact-file staging", { ok: true });
}

export function autoCommit(ctx: AutoContext, message: string): PolicyResult {
	const op = "git commit";
	const done = (reason: string, result: PolicyResult) =>
		finish(ctx, op, reason, [], result);
	const valid = validateWorktree(
		ctx.repoRoot,
		ctx.runId,
		ctx.worktreePath,
		`harness/${ctx.runId}`,
	);
	if (!valid.ok) return done(valid.reason, reject(valid.reason));
	const branch = git(valid.path, ["branch", "--show-current"]).stdout.trim();
	if (
		branch !== `harness/${ctx.runId}` ||
		branch === "main" ||
		branch === "master"
	)
		return done("wrong branch", reject(`refusing to commit on ${branch}`));
	if (git(valid.path, ["diff", "--cached", "--quiet"]).exitCode === 0)
		return done("empty index", reject("nothing staged"));
	const scan = Bun.spawnSync(["bash", SECRET_SCAN, "--cached"], {
		cwd: valid.path,
		env: cleanGitEnv(),
		stdout: "pipe",
		stderr: "pipe",
	});
	if (scan.exitCode !== 0)
		return done(
			"secret scan failed",
			reject("secret scan found staged secrets"),
		);
	if (!ctx.gatesOk())
		return done(
			"gates not green",
			reject("BUILD/TEST gates are not SUCCESS for this tree"),
		);
	// Hooks are disabled for the trusted commit; the secret scan above already ran explicitly.
	const commit = git(valid.path, [
		"-c",
		"core.hooksPath=/dev/null",
		"commit",
		"-q",
		"-m",
		message,
	]);
	if (commit.exitCode !== 0)
		return done(
			"git commit failed",
			reject(commit.stderr.trim() || "git commit failed"),
		);
	const sha = git(valid.path, ["rev-parse", "HEAD"]).stdout.trim();
	return done("local commit", { ok: true, sha });
}
