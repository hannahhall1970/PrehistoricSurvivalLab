import { appendFileSync, mkdirSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { classifyLog } from "./classify.ts";
import { type BuildLock, withBuildLock } from "./lock.ts";
import type { GateResult, HarnessConfig } from "./types.ts";

export interface Gates {
	preflight(signal?: AbortSignal): Promise<GateResult>;
	build(signal?: AbortSignal): Promise<GateResult>;
	test(signal?: AbortSignal): Promise<GateResult>;
}

const tailLimit = 32_000;
const tailAppend = (tail: string, chunk: string): string => {
	const value = tail + chunk;
	return value.length > tailLimit ? value.slice(-tailLimit) : value;
};

const killGroup = (pid: number, signal: "SIGTERM" | "SIGKILL"): void => {
	try {
		process.kill(-pid, signal);
	} catch {
		/* process already exited */
	}
};

async function execute(
	command: string[],
	cwd: string,
	logPath: string,
	timeoutMs: number,
	signal?: AbortSignal,
): Promise<{
	exitCode: number | null;
	durationMs: number;
	tail: string;
	timedOut: boolean;
}> {
	writeFileSync(logPath, "");
	const started = Date.now();
	let processHandle: Bun.Subprocess;
	try {
		processHandle = Bun.spawn(command, {
			cwd,
			stdout: "pipe",
			stderr: "pipe",
			detached: true,
		});
	} catch (error) {
		const text = String(error);
		appendFileSync(logPath, text);
		return {
			exitCode: null,
			durationMs: Date.now() - started,
			tail: text,
			timedOut: false,
		};
	}
	let tail = "";
	const consume = async (
		stream: ReadableStream<Uint8Array> | null,
	): Promise<void> => {
		if (!stream) return;
		const decoder = new TextDecoder();
		for await (const chunk of stream) {
			const text =
				typeof chunk === "string"
					? chunk
					: decoder.decode(chunk, { stream: true });
			appendFileSync(logPath, text);
			tail = tailAppend(tail, text);
		}
		const rest = decoder.decode();
		if (rest) {
			appendFileSync(logPath, rest);
			tail = tailAppend(tail, rest);
		}
	};
	const pumps = Promise.all([
		consume(
			processHandle.stdout instanceof ReadableStream
				? processHandle.stdout
				: null,
		),
		consume(
			processHandle.stderr instanceof ReadableStream
				? processHandle.stderr
				: null,
		),
	]);
	let timer: ReturnType<typeof setTimeout> | undefined;
	let abortHandler: (() => void) | undefined;
	const stopPromise = new Promise<"stop">((resolve) => {
		timer = setTimeout(() => resolve("stop"), timeoutMs);
		abortHandler = () => resolve("stop");
		if (signal?.aborted) resolve("stop");
		else signal?.addEventListener("abort", abortHandler, { once: true });
	});
	const result = await Promise.race([
		processHandle.exited.then((exitCode) => ({
			kind: "exit" as const,
			exitCode,
		})),
		stopPromise.then(() => ({ kind: "stop" as const })),
	]);
	if (timer) clearTimeout(timer);
	if (abortHandler) signal?.removeEventListener("abort", abortHandler);
	if (result.kind === "stop") {
		killGroup(processHandle.pid, "SIGTERM");
		const killTimer = setTimeout(
			() => killGroup(processHandle.pid, "SIGKILL"),
			10_000,
		);
		await processHandle.exited.catch(() => undefined);
		clearTimeout(killTimer);
		await pumps;
		return {
			exitCode: null,
			durationMs: Date.now() - started,
			tail,
			timedOut: true,
		};
	}
	await pumps;
	return {
		exitCode: result.exitCode,
		durationMs: Date.now() - started,
		tail,
		timedOut: false,
	};
}

export class CommandGates implements Gates {
	private readonly repoRoot: string;
	private readonly config: HarnessConfig;
	private readonly logDir: string;
	private readonly lock: BuildLock;
	constructor(opts: {
		repoRoot: string;
		config: HarnessConfig;
		logDir: string;
		lock: BuildLock;
	}) {
		this.repoRoot = opts.repoRoot;
		this.config = opts.config;
		this.logDir = opts.logDir;
		this.lock = opts.lock;
		mkdirSync(this.logDir, { recursive: true });
	}

	preflight(signal?: AbortSignal): Promise<GateResult> {
		return this.run(
			"PREFLIGHT",
			this.config.commands.preflight,
			this.config.limits.preflightTimeoutMs,
			signal,
		);
	}

	build(signal?: AbortSignal): Promise<GateResult> {
		return this.runLocked(
			"BUILD",
			this.config.commands.build,
			this.config.limits.buildTimeoutMs,
			signal,
		);
	}

	test(signal?: AbortSignal): Promise<GateResult> {
		return this.runLocked(
			"TEST",
			this.config.commands.test,
			this.config.limits.testTimeoutMs,
			signal,
		);
	}

	private runLocked(
		gate: "BUILD" | "TEST",
		command: string[],
		timeoutMs: number,
		signal?: AbortSignal,
	): Promise<GateResult> {
		const owner = `${process.pid}:${gate}:${Date.now()}`;
		return withBuildLock(this.lock, owner, () =>
			this.run(gate, command, timeoutMs, signal),
		).then((result) => {
			if (result) return result;
			const logPath = this.logPath(gate);
			const holder = this.lock.holder();
			const summary = holder
				? `Build lock held by ${holder.owner}`
				: "Build lock unavailable";
			writeFileSync(logPath, summary);
			return {
				gate,
				outcome: "FAILURE",
				exitCode: null,
				durationMs: 0,
				command: [...command],
				logPath,
				failure: { cls: "BUILD_LOCKED", summary, logPath },
			} satisfies GateResult;
		});
	}

	private async run(
		gate: "PREFLIGHT" | "BUILD" | "TEST",
		command: string[],
		timeoutMs: number,
		signal?: AbortSignal,
	): Promise<GateResult> {
		const logPath = this.logPath(gate);
		const executed = await execute(
			command,
			this.repoRoot,
			logPath,
			timeoutMs,
			signal,
		);
		if (executed.timedOut) {
			return {
				gate,
				outcome: "TIMEOUT",
				exitCode: null,
				durationMs: executed.durationMs,
				command: [...command],
				logPath,
				failure: {
					cls: "TIMEOUT",
					summary: `${gate} timed out`,
					excerpt: executed.tail.split(/\r?\n/).slice(-40).join("\n"),
					logPath,
				},
			};
		}
		if (executed.exitCode === 0)
			return {
				gate,
				outcome: "SUCCESS",
				exitCode: 0,
				durationMs: executed.durationMs,
				command: [...command],
				logPath,
			};
		const classified = classifyLog(gate, executed.exitCode, executed.tail);
		return {
			gate,
			outcome: classified.cls === "TOOLCHAIN_MISSING" ? "BLOCKED" : "FAILURE",
			exitCode: executed.exitCode,
			durationMs: executed.durationMs,
			command: [...command],
			logPath,
			failure: { ...classified, logPath },
		};
	}

	private logPath(gate: string): string {
		return join(this.logDir, `${gate.toLowerCase()}-${Date.now()}.log`);
	}
}
