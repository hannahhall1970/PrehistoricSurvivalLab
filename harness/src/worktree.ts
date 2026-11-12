import { lstatSync, realpathSync, type Stats } from "node:fs";
import { isAbsolute, join, resolve } from "node:path";

export type WorktreeValidation =
	| { ok: true; path: string }
	| { ok: false; reason: string };

export class WorktreeValidationError extends Error {
	readonly reason: string;

	constructor(reason: string) {
		super(reason);
		this.name = "WorktreeValidationError";
		this.reason = reason;
	}
}

type GitResult = {
	exitCode: number;
	stdout: string;
	stderr: string;
};

const text = (value: unknown): string =>
	typeof value === "string"
		? value
		: value instanceof Uint8Array
			? new TextDecoder().decode(value)
			: "";

/** Repo-selection overrides (GIT_DIR, GIT_WORK_TREE, GIT_INDEX_FILE, ...) must never redirect harness git calls. */
export const cleanGitEnv = (): Record<string, string> =>
	Object.fromEntries(
		Object.entries(process.env).filter(
			(entry): entry is [string, string] =>
				entry[1] !== undefined && !entry[0].startsWith("GIT_"),
		),
	);

const git = (cwd: string, args: string[]): GitResult => {
	try {
		const result = Bun.spawnSync(["git", "-C", cwd, ...args], {
			env: cleanGitEnv(),
			stdout: "pipe",
			stderr: "pipe",
		});
		return {
			exitCode: result.exitCode,
			stdout: text(result.stdout),
			stderr: text(result.stderr),
		};
	} catch (error) {
		return {
			exitCode: 1,
			stdout: "",
			stderr: error instanceof Error ? error.message : String(error),
		};
	}
};

const failure = (check: string, detail: string): WorktreeValidation => ({
	ok: false,
	reason: `worktree validation failed (${check}): ${detail}`,
});

const commandError = (result: GitResult): string =>
	(result.stderr || result.stdout || "command failed").trim();

export function validateWorktree(
	repoRoot: string,
	runId: string,
	worktreePath: string | undefined,
	branch: string | undefined,
): WorktreeValidation {
	if (!/^[0-9a-f-]{36}$/.test(runId))
		return failure("runId", `invalid run id ${JSON.stringify(runId)}`);
	if (!worktreePath)
		return failure("path", "persisted worktreePath is missing");

	let repo: string;
	try {
		repo = realpathSync(repoRoot);
	} catch (error) {
		return failure(
			"repo-root",
			`cannot resolve ${repoRoot}: ${error instanceof Error ? error.message : String(error)}`,
		);
	}
	const expected = join(repo, ".harness", "worktrees", runId);

	let worktreeStat: Stats;
	try {
		worktreeStat = lstatSync(worktreePath);
	} catch (error) {
		return failure(
			"path",
			`persisted path ${worktreePath} is unavailable; expected ${expected}: ${error instanceof Error ? error.message : String(error)}`,
		);
	}
	if (worktreeStat.isSymbolicLink())
		return failure(
			"path",
			`persisted worktree path is a symlink: ${worktreePath}`,
		);
	if (!worktreeStat.isDirectory())
		return failure(
			"path",
			`persisted worktree path is not a directory: ${worktreePath}`,
		);

	for (const [label, path] of [
		[".harness", join(repo, ".harness")],
		[".harness/worktrees", join(repo, ".harness", "worktrees")],
	] as const) {
		let stat: Stats;
		try {
			stat = lstatSync(path);
		} catch (error) {
			return failure(
				label,
				`expected real directory ${path} is unavailable: ${error instanceof Error ? error.message : String(error)}`,
			);
		}
		if (stat.isSymbolicLink())
			return failure(label, `expected real directory is a symlink: ${path}`);
		if (!stat.isDirectory())
			return failure(
				label,
				`expected real directory is not a directory: ${path}`,
			);
	}

	let resolvedWorktree: string;
	try {
		resolvedWorktree = realpathSync(worktreePath);
	} catch (error) {
		return failure(
			"path",
			`cannot resolve persisted path ${worktreePath}; expected ${expected}: ${error instanceof Error ? error.message : String(error)}`,
		);
	}
	if (resolvedWorktree !== expected)
		return failure(
			"path",
			`resolved persisted path ${resolvedWorktree} does not equal expected ${expected}`,
		);

	if (branch !== undefined && branch !== `harness/${runId}`)
		return failure(
			"persisted branch",
			`expected harness/${runId}, received ${JSON.stringify(branch)}`,
		);

	const top = git(expected, ["rev-parse", "--show-toplevel"]);
	if (top.exitCode !== 0)
		return failure(
			"Git worktree",
			`rev-parse --show-toplevel failed: ${commandError(top)}`,
		);
	let topReal: string;
	try {
		topReal = realpathSync(top.stdout.trim());
	} catch (error) {
		return failure(
			"Git worktree",
			`cannot resolve Git top-level ${top.stdout.trim()}: ${error instanceof Error ? error.message : String(error)}`,
		);
	}
	if (topReal !== expected)
		return failure(
			"Git worktree",
			`Git top-level ${topReal} does not equal expected ${expected}`,
		);

	const worktreeCommon = git(expected, ["rev-parse", "--git-common-dir"]);
	if (worktreeCommon.exitCode !== 0)
		return failure(
			"common-dir",
			`worktree common-dir lookup failed: ${commandError(worktreeCommon)}`,
		);
	const repoCommon = git(repo, ["rev-parse", "--git-common-dir"]);
	if (repoCommon.exitCode !== 0)
		return failure(
			"common-dir",
			`repository common-dir lookup failed: ${commandError(repoCommon)}`,
		);
	let worktreeCommonReal: string;
	let repoCommonReal: string;
	try {
		const worktreeCommonPath = worktreeCommon.stdout.trim();
		const repoCommonPath = repoCommon.stdout.trim();
		worktreeCommonReal = realpathSync(
			isAbsolute(worktreeCommonPath)
				? worktreeCommonPath
				: resolve(expected, worktreeCommonPath),
		);
		repoCommonReal = realpathSync(
			isAbsolute(repoCommonPath)
				? repoCommonPath
				: resolve(repo, repoCommonPath),
		);
	} catch (error) {
		return failure(
			"common-dir",
			`cannot resolve Git common-dir: ${error instanceof Error ? error.message : String(error)}`,
		);
	}
	if (worktreeCommonReal !== repoCommonReal)
		return failure(
			"common-dir",
			`worktree ${worktreeCommonReal} does not match repository ${repoCommonReal}`,
		);

	const listed = git(repo, ["worktree", "list", "--porcelain"]);
	if (listed.exitCode !== 0)
		return failure(
			"registration",
			`git worktree list failed: ${commandError(listed)}`,
		);
	let registered = false;
	for (const block of listed.stdout.split(/\r?\n\s*\r?\n/)) {
		const entry = block
			.split(/\r?\n/)
			.find((line) => line.startsWith("worktree "));
		if (!entry) continue;
		const listedPath = entry.slice("worktree ".length).trim();
		let listedReal: string;
		try {
			listedReal = realpathSync(listedPath);
		} catch {
			continue;
		}
		if (
			listedReal === expected &&
			!block.split(/\r?\n/).some((line) => /^\s*prunable(?:\s|$)/.test(line))
		) {
			registered = true;
			break;
		}
	}
	if (!registered)
		return failure(
			"registration",
			`expected ${expected} is not a non-prunable registered worktree`,
		);

	const actualBranch = git(expected, ["branch", "--show-current"]);
	if (actualBranch.exitCode !== 0)
		return failure(
			"branch",
			`branch lookup failed: ${commandError(actualBranch)}`,
		);
	const expectedBranch = `harness/${runId}`;
	if (actualBranch.stdout.trim() !== expectedBranch)
		return failure(
			"branch",
			`expected ${expectedBranch}, received ${JSON.stringify(actualBranch.stdout.trim())}`,
		);

	return { ok: true, path: expected };
}
