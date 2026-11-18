import { describe, expect, it } from "bun:test";
import {
	existsSync,
	mkdirSync,
	mkdtempSync,
	rmSync,
	writeFileSync,
} from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { CommandGates } from "../src/gates.ts";
import { BuildLock } from "../src/lock.ts";
import type { HarnessConfig } from "../src/types.ts";

const makeConfig = (command: string[], timeout = 1000): HarnessConfig => ({
	daemonUrl: "ws://127.0.0.1:1/ws",
	roles: {
		planner: { provider: "omp/test", modeId: "full" },
		implementer: { provider: "omp/test", modeId: "full" },
		reviewer: { provider: "omp/test", modeId: "ask" },
	},
	limits: {
		defaultMaxAttempts: 1,
		defaultBudgetMs: 1000,
		buildTimeoutMs: timeout,
		testTimeoutMs: timeout,
		preflightTimeoutMs: timeout,
		maxTasksPerRun: 1,
	},
	commands: { preflight: command, build: command, test: command },
	useWorktrees: false,
});

const setup = () => {
	const root = mkdtempSync(join(tmpdir(), "psl-gates-"));
	const logDir = join(root, "logs");
	mkdirSync(logDir);
	return { root, logDir, lock: new BuildLock(join(root, "build.lock")) };
};

describe("CommandGates", () => {
	it("classifies non-zero command output and writes complete logs", async () => {
		const env = setup();
		const gates = new CommandGates({
			repoRoot: env.root,
			config: makeConfig(["bash", "-c", "echo error: x; exit 2"]),
			logDir: env.logDir,
			lock: env.lock,
		});
		const result = await gates.preflight();
		expect(result.outcome).toBe("FAILURE");
		expect(result.exitCode).toBe(2);
		expect(result.failure?.cls).toBe("UNKNOWN");
		expect(await Bun.file(result.logPath).text()).toContain("error: x");
		rmSync(env.root, { recursive: true, force: true });
	});

	it("kills timed out commands and returns null exit code", async () => {
		const env = setup();
		const gates = new CommandGates({
			repoRoot: env.root,
			config: makeConfig(["bash", "-c", "sleep 2"], 40),
			logDir: env.logDir,
			lock: env.lock,
		});
		const result = await gates.preflight();
		expect(result.outcome).toBe("TIMEOUT");
		expect(result.exitCode).toBeNull();
		expect(result.failure?.cls).toBe("TIMEOUT");
		rmSync(env.root, { recursive: true, force: true });
	});

	it("returns promptly when a grandchild ignores SIGTERM and holds the pipes", async () => {
		const env = setup();
		const marker = join(env.root, "survivor.pid");
		const gates = new CommandGates({
			repoRoot: env.root,
			config: makeConfig(
				[
					"sh",
					"-c",
					`trap "" TERM; (trap "" TERM; echo $$ > ${marker}; exec sleep 30) & wait`,
				],
				200,
			),
			logDir: env.logDir,
			lock: env.lock,
			killGraceMs: 300,
		});
		const started = Date.now();
		const result = await gates.preflight();
		const elapsed = Date.now() - started;
		console.log(`grandchild gate elapsed=${elapsed}ms`);
		expect(result.outcome).toBe("TIMEOUT");
		expect(elapsed).toBeLessThan(3000);
		const pid = Number((await Bun.file(marker).text()).trim());
		expect(pid).toBeGreaterThan(0);
		let alive = true;
		try {
			process.kill(pid, 0);
		} catch {
			alive = false;
		}
		expect(alive).toBe(false);
		rmSync(env.root, { recursive: true, force: true });
	});

	it("reports external abort as CANCELLED, not TIMEOUT", async () => {
		const env = setup();
		const gates = new CommandGates({
			repoRoot: env.root,
			config: makeConfig(["bash", "-c", "sleep 5"], 10_000),
			logDir: env.logDir,
			lock: env.lock,
			killGraceMs: 300,
		});
		const controller = new AbortController();
		setTimeout(() => controller.abort(), 50);
		const result = await gates.preflight(controller.signal);
		expect(result.failure?.cls).toBe("CANCELLED");
		expect(result.outcome).toBe("FAILURE");
		rmSync(env.root, { recursive: true, force: true });
	});

	it("reports a live lock as BUILD_LOCKED", async () => {
		const env = setup();
		env.lock.tryAcquire("other");
		const gates = new CommandGates({
			repoRoot: env.root,
			config: makeConfig(["bash", "-c", "exit 0"]),
			logDir: env.logDir,
			lock: env.lock,
		});
		const result = await gates.build();
		expect(result.outcome).toBe("FAILURE");
		expect(result.failure?.cls).toBe("BUILD_LOCKED");
		env.lock.release("other");
		rmSync(env.root, { recursive: true, force: true });
	});

	it("reclaims stale locks before building", async () => {
		const env = setup();
		writeFileSync(
			join(env.root, "build.lock"),
			JSON.stringify({
				owner: "dead",
				pid: 999999,
				at: new Date().toISOString(),
			}),
		);
		const gates = new CommandGates({
			repoRoot: env.root,
			config: makeConfig(["bash", "-c", "echo ok"]),
			logDir: env.logDir,
			lock: env.lock,
		});
		const result = await gates.build();
		expect(result.outcome).toBe("SUCCESS");
		expect(env.lock.holder()).toBeNull();
		rmSync(env.root, { recursive: true, force: true });
	});
	it("blocks differing worktree scripts without executing them, but runs identical scripts", async () => {
		const env = setup();
		const worktree = join(env.root, "worktree");
		const trustedScripts = join(env.root, "scripts");
		const worktreeScripts = join(worktree, "scripts");
		const marker = join(worktree, "marker.txt");
		const trustedScript = "#!/bin/sh\nprintf trusted > marker.txt\n";
		try {
			mkdirSync(trustedScripts, { recursive: true });
			mkdirSync(worktreeScripts, { recursive: true });
			writeFileSync(join(trustedScripts, "build.sh"), trustedScript, {
				mode: 0o755,
			});
			writeFileSync(
				join(worktreeScripts, "build.sh"),
				"#!/bin/sh\nprintf forged > marker.txt\n",
				{ mode: 0o755 },
			);
			const gates = new CommandGates({
				repoRoot: env.root,
				config: makeConfig(["scripts/build.sh"]),
				logDir: env.logDir,
				lock: env.lock,
			});

			const blocked = await gates.build(undefined, worktree);
			expect(blocked.outcome).toBe("BLOCKED");
			expect(existsSync(marker)).toBe(false);

			writeFileSync(join(worktreeScripts, "build.sh"), trustedScript, {
				mode: 0o755,
			});
			const executed = await gates.build(undefined, worktree);
			expect(executed.outcome).toBe("SUCCESS");
			expect(Bun.file(marker).text()).resolves.toBe("trusted");
		} finally {
			rmSync(env.root, { recursive: true, force: true });
		}
	});
});
