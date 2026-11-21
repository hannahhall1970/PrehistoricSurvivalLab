import { afterEach, describe, expect, it } from "bun:test";
import { mkdirSync, mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import {
	type ApprovalIO,
	type ApprovalTarget,
	confirmTransaction,
	type ReleaseTransaction,
	requireApproval,
} from "../src/approval.ts";
import { main } from "../src/cli.ts";
import {
	type AutoContext,
	authorize,
	autoCommit,
	autoStage,
	classifyOperation,
} from "../src/policy.ts";

const roots: string[] = [];

afterEach(() => {
	for (const root of roots.splice(0))
		rmSync(root, { recursive: true, force: true });
});

function command(cwd: string, args: string[]): string {
	const result = Bun.spawnSync(args, { cwd, stdout: "pipe", stderr: "pipe" });
	if (result.exitCode !== 0)
		throw new Error(
			`${args.join(" ")}: ${new TextDecoder().decode(result.stderr)}`,
		);
	return new TextDecoder().decode(result.stdout);
}

function makeRepo(): string {
	const root = mkdtempSync(join(tmpdir(), "psl-policy-"));
	roots.push(root);
	const origin = join(root, "origin.git");
	const repo = join(root, "repo");
	command(root, ["git", "init", "--bare", "-q", origin]);
	command(root, ["git", "init", "-q", repo]);
	command(repo, ["git", "config", "user.email", "harness@example.test"]);
	command(repo, ["git", "config", "user.name", "Harness Test"]);
	writeFileSync(join(repo, "tracked.txt"), "base\n");
	writeFileSync(join(repo, ".gitignore"), ".harness/\n");
	command(repo, ["git", "add", "tracked.txt", ".gitignore"]);
	command(repo, ["git", "commit", "-qm", "base"]);
	command(repo, ["git", "branch", "-M", "main"]);
	command(repo, ["git", "remote", "add", "origin", origin]);
	command(repo, ["git", "push", "-q", "-u", "origin", "main"]);
	return repo;
}

function addWorktree(repo: string, id = crypto.randomUUID()) {
	const path = join(repo, ".harness", "worktrees", id);
	mkdirSync(join(repo, ".harness", "worktrees"), { recursive: true });
	command(repo, [
		"git",
		"worktree",
		"add",
		"-q",
		"-b",
		`harness/${id}`,
		path,
		"origin/main",
	]);
	return { id, path };
}

type LogEvent = Parameters<AutoContext["log"]>[0];

function fixture(declaredFiles = ["src.txt"], gatesOk = true) {
	const repo = makeRepo();
	const wt = addWorktree(repo);
	const events: LogEvent[] = [];
	const ctx: AutoContext = {
		repoRoot: repo,
		runId: wt.id,
		worktreePath: wt.path,
		declaredFiles,
		log: (event) => events.push(event),
		gatesOk: () => gatesOk,
	};
	return { repo, wt, ctx, events };
}

const staged = (cwd: string) =>
	command(cwd, ["git", "diff", "--cached", "--name-only"]).trim();
const blobOf = (cwd: string, path: string) =>
	command(cwd, ["git", "hash-object", "--", path]).trim();

const SHA_X = "a".repeat(40);
const SHA_Y = "b".repeat(40);

function transaction(runId = "run-1"): ReleaseTransaction {
	return {
		runId,
		runVersion: 3,
		sourceBranch: `harness/${runId}`,
		targetBranch: "main",
		sourceSha: SHA_X,
		targetSha: SHA_X,
		remoteUrl: "git@example.test:psl.git",
		files: ["src.txt"],
		lfsObjects: [],
		operations: ["git.merge", "git.push"],
	};
}

function requestFor(
	tx: ReleaseTransaction,
	operation: "git.merge" | "git.push",
): ApprovalTarget {
	return {
		operation,
		runId: tx.runId,
		runVersion: tx.runVersion,
		target: tx.targetBranch,
		sourceSha: tx.sourceSha,
		targetSha: tx.targetSha,
		remoteUrl: tx.remoteUrl,
	};
}

function scriptedHuman(): { io: ApprovalIO; asks: string[] } {
	const asks: string[] = [];
	return {
		asks,
		io: {
			ask(prompt) {
				asks.push(prompt);
				return prompt.match(/Type ([0-9A-F]{6}) /)?.[1];
			},
		},
	};
}

describe("PSL.policy 1 exact-file staging", () => {
	it("auto-approves exact-file staging and logs an ok auto_op", () => {
		const { wt, ctx, events } = fixture();
		writeFileSync(join(wt.path, "src.txt"), "hello\n");
		const result = autoStage(ctx, ["src.txt"], {
			"src.txt": blobOf(wt.path, "src.txt"),
		});
		expect(result.ok).toBe(true);
		expect(staged(wt.path)).toBe("src.txt");
		expect(classifyOperation("git add -- src.txt")).toBe("AUTO");
		expect(events).toHaveLength(1);
		expect(events[0]?.kind).toBe("auto_op");
		expect(events[0]?.data).toMatchObject({
			op: "git add --",
			paths: ["src.txt"],
			runId: ctx.runId,
			outcome: "ok",
		});
	});
});

describe("PSL.policy 2 unexpected staging is rejected", () => {
	it("rejects undeclared, mismatched, wildcard and traversal paths and stages nothing", () => {
		const { wt, ctx, events } = fixture();
		writeFileSync(join(wt.path, "src.txt"), "hello\n");
		writeFileSync(join(wt.path, "other.txt"), "other\n");
		const blobs = {
			"src.txt": blobOf(wt.path, "src.txt"),
			"other.txt": blobOf(wt.path, "other.txt"),
		};
		const attempts: Array<[string[], Record<string, string>]> = [
			[["other.txt"], blobs], // not declared
			[["src.txt"], { "src.txt": "0".repeat(40) }], // blob mismatch
			[["src.txt", "other.txt"], blobs], // one undeclared poisons the set
			[["-A"], blobs],
			[["."], blobs],
			[["*.txt"], blobs],
			[["src.*"], blobs],
			[["../escape.txt"], blobs],
			[["src/../../escape.txt"], blobs],
			[["/etc/passwd"], blobs],
		];
		for (const [paths, expected] of attempts) {
			const result = autoStage(ctx, paths, expected);
			expect(result.ok).toBe(false);
			expect(staged(wt.path)).toBe("");
		}
		expect(events.every((e) => e.data.outcome === "rejected")).toBe(true);
		expect(events).toHaveLength(attempts.length);
		for (const op of [
			"git add -A",
			"git add .",
			"git add *.ts",
			"git add --all",
			"git add -u",
		])
			expect(classifyOperation(op)).toBe("DENY");
	});
});

describe("PSL.policy 3 local commit on harness branch", () => {
	it("commits when gates and the secret scan are clean", () => {
		const { wt, ctx, events } = fixture();
		writeFileSync(join(wt.path, "src.txt"), "hello\n");
		expect(
			autoStage(ctx, ["src.txt"], { "src.txt": blobOf(wt.path, "src.txt") }).ok,
		).toBe(true);
		const result = autoCommit(ctx, "add src");
		expect(result.ok).toBe(true);
		expect(command(wt.path, ["git", "rev-parse", "HEAD"]).trim()).toBe(
			result.ok ? (result.sha ?? "") : "",
		);
		expect(command(wt.path, ["git", "branch", "--show-current"]).trim()).toBe(
			`harness/${ctx.runId}`,
		);
		expect(
			classifyOperation("git commit -m x", {
				runId: ctx.runId,
				branch: `harness/${ctx.runId}`,
			}),
		).toBe("AUTO");
		expect(events.at(-1)?.data).toMatchObject({
			op: "git commit",
			outcome: "ok",
		});
	});

	it("refuses an empty index, red gates, and staged secrets", () => {
		const empty = fixture();
		expect(autoCommit(empty.ctx, "nothing").ok).toBe(false);

		const red = fixture(["src.txt"], false);
		writeFileSync(join(red.wt.path, "src.txt"), "hello\n");
		autoStage(red.ctx, ["src.txt"], {
			"src.txt": blobOf(red.wt.path, "src.txt"),
		});
		const before = command(red.wt.path, ["git", "rev-parse", "HEAD"]);
		expect(autoCommit(red.ctx, "red").ok).toBe(false);
		expect(command(red.wt.path, ["git", "rev-parse", "HEAD"])).toBe(before);

		const leaky = fixture();
		writeFileSync(
			join(leaky.wt.path, "src.txt"),
			`token=ghp_${"a1".repeat(15)}\n`,
		);
		autoStage(leaky.ctx, ["src.txt"], {
			"src.txt": blobOf(leaky.wt.path, "src.txt"),
		});
		const leak = autoCommit(leaky.ctx, "leak");
		expect(leak.ok).toBe(false);
		expect(leak.ok ? "" : leak.reason).toContain("secret");
	});
});

describe("PSL.policy 4 commit on main is blocked", () => {
	it("refuses to commit from the main checkout", () => {
		const { repo, ctx } = fixture();
		writeFileSync(join(repo, "src.txt"), "hello\n");
		command(repo, ["git", "add", "--", "src.txt"]);
		const before = command(repo, ["git", "rev-parse", "HEAD"]);
		const onMain = { ...ctx, worktreePath: repo };
		expect(autoCommit(onMain, "on main").ok).toBe(false);
		expect(command(repo, ["git", "rev-parse", "HEAD"])).toBe(before);
		expect(
			classifyOperation("git commit -m x", {
				runId: ctx.runId,
				branch: "main",
			}),
		).toBe("ASK");
		expect(
			classifyOperation("git commit -m x", {
				runId: ctx.runId,
				branch: "master",
			}),
		).toBe("ASK");
	});
});

describe("PSL.policy 5 remote push needs a trusted grant", () => {
	it("blocks without a grant and allows exactly once with a transaction grant", () => {
		const tx = transaction();
		const request = requestFor(tx, "git.push");
		expect(classifyOperation("git push origin main")).toBe("ASK");
		expect(authorize("git push origin main", request, undefined).approved).toBe(
			false,
		);

		const grants = confirmTransaction(tx, scriptedHuman().io);
		const grant = grants?.get("git.push");
		expect(grant).toBeDefined();
		expect(authorize("git push origin main", request, grant).approved).toBe(
			true,
		);
		expect(authorize("git push origin main", request, grant).approved).toBe(
			false,
		);
	});
});

describe("PSL.policy 6 destructive operations are DENY", () => {
	it("stays DENY even when a valid grant is supplied", () => {
		const tx = transaction();
		const ops = [
			"git push --force origin main",
			"git push -f origin main",
			"git push --force-with-lease origin main",
			"git push origin -uf main",
			"git push -fu origin main",
			"git push -fn origin main",
			"git push origin -d main",
			"git push origin :main",
			"git push origin +main",
			"git push --mirror origin",
			"git push --delete origin main",
			"git push --prune origin main",
			"git reset --hard HEAD~1",
			"git rebase main",
			"git rebase -i HEAD~3",
			"git clean -fdx",
			"git clean -fd",
		];
		for (const op of ops) {
			expect(classifyOperation(op)).toBe("DENY");
			const grant = confirmTransaction(tx, scriptedHuman().io)?.get("git.push");
			const decision = authorize(op, requestFor(tx, "git.push"), grant);
			expect(decision.approved).toBe(false);
			expect(decision.reason).toContain("DENY");
		}
	});

	it("classifies the AUTO and ASK tiers", () => {
		for (const op of ["git status", "git diff", "build", "test", "preflight"])
			expect(classifyOperation(op)).toBe("AUTO");
		for (const op of [
			"git merge harness/x",
			"git push origin main",
			"npm install",
			"sudo ls",
			"git branch -D x",
		])
			expect(classifyOperation(op)).toBe("ASK");
	});
});

describe("PSL.policy 7 SHA mismatch invalidates approval", () => {
	it("rejects a grant for SHA X when the request carries Y, and consumes it", () => {
		const tx = transaction();
		const grant = confirmTransaction(tx, scriptedHuman().io)?.get("git.push");
		const wrong = { ...requestFor(tx, "git.push"), targetSha: SHA_Y };
		const decision = requireApproval(wrong, grant);
		expect(decision.approved).toBe(false);
		expect(decision.reason).toContain("targetSha");
		const retry = requireApproval(requestFor(tx, "git.push"), grant);
		expect(retry.approved).toBe(false);
		expect(retry.reason).toContain("not issued");

		const g2 = confirmTransaction(tx, scriptedHuman().io)?.get("git.merge");
		const badSource = { ...requestFor(tx, "git.merge"), sourceSha: SHA_Y };
		expect(requireApproval(badSource, g2).reason).toContain("sourceSha");
		const g3 = confirmTransaction(tx, scriptedHuman().io)?.get("git.push");
		const badRemote = {
			...requestFor(tx, "git.push"),
			remoteUrl: "git@evil.test:x.git",
		};
		expect(requireApproval(badRemote, g3).reason).toContain("remoteUrl");
		const g4 = confirmTransaction(tx, scriptedHuman().io)?.get("git.push");
		const { targetSha: _omit, ...noSha } = requestFor(tx, "git.push");
		expect(requireApproval(noSha, g4).approved).toBe(false);
	});
});

describe("PSL.policy 8 worker cannot mint its own grant", () => {
	it("rejects forged objects, JSON copies, --approve-op and free-text approve", async () => {
		const tx = transaction();
		const request = requestFor(tx, "git.push");
		const forged = {
			operation: "git.push",
			approved: true,
			source: "tty-challenge",
			runId: tx.runId,
			runVersion: tx.runVersion,
			nonce: "ABCDEF",
			target: tx.targetBranch,
			sourceSha: tx.sourceSha,
			targetSha: tx.targetSha,
			remoteUrl: tx.remoteUrl,
		};
		expect(authorize("git push origin main", request, forged).approved).toBe(
			false,
		);

		const real = confirmTransaction(tx, scriptedHuman().io)?.get("git.push");
		const copy = JSON.parse(JSON.stringify(real));
		expect(requireApproval(request, copy).reason).toContain(
			"not issued by the trusted approval source",
		);
		expect(requireApproval(request, { ...(real as object) }).approved).toBe(
			false,
		);

		expect(await main(["resume", tx.runId, "--approve-op", "git.push"])).toBe(
			1,
		);
		expect(await main(["resume", tx.runId, "--approve-op=git.push"])).toBe(1);

		for (const text of ["approve", "approved", "yes", true, { approved: true }])
			expect(authorize("git push origin main", request, text).approved).toBe(
				false,
			);
		expect(confirmTransaction(tx, { ask: () => "approve" })).toBeUndefined();
		expect(confirmTransaction(tx, { ask: () => undefined })).toBeUndefined();
	});
});

describe("PSL.policy 9 one prompt per transaction", () => {
	it("asks once for merge+push and validates both operations", () => {
		const tx = transaction();
		const { io, asks } = scriptedHuman();
		const grants = confirmTransaction(tx, io);
		expect(asks).toHaveLength(1);
		for (const needle of [
			tx.sourceBranch,
			tx.targetBranch,
			tx.sourceSha,
			tx.remoteUrl,
			"src.txt",
			"git.merge",
			"git.push",
		])
			expect(asks[0]).toContain(needle);
		expect(grants?.size).toBe(2);
		expect(grants?.get("git.merge")).not.toBe(grants?.get("git.push"));
		expect(
			requireApproval(requestFor(tx, "git.merge"), grants?.get("git.merge")),
		).toEqual({ approved: true });
		expect(
			requireApproval(requestFor(tx, "git.push"), grants?.get("git.push")),
		).toEqual({ approved: true });
		expect(asks).toHaveLength(1);
		// a merge grant never authorizes the push
		const again = confirmTransaction(tx, io);
		expect(
			requireApproval(requestFor(tx, "git.push"), again?.get("git.merge"))
				.approved,
		).toBe(false);
	});
});
