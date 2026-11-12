import { describe, expect, it } from "bun:test";
import {
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
});
