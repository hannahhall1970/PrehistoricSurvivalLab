import { existsSync, readFileSync } from "node:fs";
import { join } from "node:path";
import type { HarnessConfig, Role, RoleConfig } from "./types.ts";

const DEFAULT_ROLES: Record<Role, RoleConfig> = {
	planner: {
		provider: "omp/circle-ai/claude-opus-5-5",
		modeId: "full",
		thinkingOptionId: "high",
	},
	implementer: {
		provider: "omp/openai-codex/gpt-6.1-sol",
		modeId: "full",
		thinkingOptionId: "high",
	},
	reviewer: {
		provider: "omp/cliproxy/codex-auto-review",
		modeId: "full",
		thinkingOptionId: "high",
	},
};

const DEFAULT_CONFIG: HarnessConfig = {
	daemonUrl: "ws://127.0.0.1:6767/ws",
	roles: DEFAULT_ROLES,
	limits: {
		defaultMaxAttempts: 3,
		defaultBudgetMs: 20 * 60 * 1000,
		buildTimeoutMs: 60 * 60 * 1000,
		testTimeoutMs: 30 * 60 * 1000,
		preflightTimeoutMs: 2 * 60 * 1000,
		maxTasksPerRun: 12,
	},
	commands: {
		preflight: ["bash", "scripts/audit_env.sh"],
		build: ["bash", "scripts/build.sh"],
		test: ["bash", "scripts/run_tests.sh"],
	},
	useWorktrees: false,
};

function isRecord(value: unknown): value is Record<string, unknown> {
	return typeof value === "object" && value !== null && !Array.isArray(value);
}

function positiveNumber(value: unknown, fallback: number): number {
	return typeof value === "number" && Number.isFinite(value) && value > 0
		? value
		: fallback;
}

function command(value: unknown, fallback: string[]): string[] {
	return Array.isArray(value) &&
		value.length > 0 &&
		value.every((part) => typeof part === "string" && part.length > 0)
		? [...value]
		: [...fallback];
}

function role(value: unknown, fallback: RoleConfig): RoleConfig {
	if (!isRecord(value)) return { ...fallback };
	return {
		provider:
			typeof value.provider === "string" && value.provider.length > 0
				? value.provider
				: fallback.provider,
		modeId:
			typeof value.modeId === "string" && value.modeId.length > 0
				? value.modeId
				: fallback.modeId,
		...(typeof value.thinkingOptionId === "string" &&
		value.thinkingOptionId.length > 0
			? { thinkingOptionId: value.thinkingOptionId }
			: fallback.thinkingOptionId
				? { thinkingOptionId: fallback.thinkingOptionId }
				: {}),
	};
}

export function defaultConfig(): HarnessConfig {
	return {
		daemonUrl: DEFAULT_CONFIG.daemonUrl,
		roles: {
			planner: { ...DEFAULT_CONFIG.roles.planner },
			implementer: { ...DEFAULT_CONFIG.roles.implementer },
			reviewer: { ...DEFAULT_CONFIG.roles.reviewer },
		},
		limits: { ...DEFAULT_CONFIG.limits },
		commands: {
			preflight: [...DEFAULT_CONFIG.commands.preflight],
			build: [...DEFAULT_CONFIG.commands.build],
			test: [...DEFAULT_CONFIG.commands.test],
		},
		useWorktrees: DEFAULT_CONFIG.useWorktrees,
	};
}

export function configPath(repoRoot: string): string {
	return join(repoRoot, "harness", "harness.config.json");
}

export function loadConfig(repoRoot: string): HarnessConfig {
	const defaults = defaultConfig();
	const filename = configPath(repoRoot);
	if (!existsSync(filename)) return defaults;

	let parsed: unknown;
	try {
		parsed = JSON.parse(readFileSync(filename, "utf8"));
	} catch (error) {
		const detail = error instanceof Error ? error.message : String(error);
		throw new Error(`Invalid harness config ${filename}: ${detail}`, {
			cause: error,
		});
	}
	if (!isRecord(parsed))
		throw new Error(`Harness config must be a JSON object: ${filename}`);

	const limits = isRecord(parsed.limits) ? parsed.limits : {};
	const commands = isRecord(parsed.commands) ? parsed.commands : {};
	const roles = isRecord(parsed.roles) ? parsed.roles : {};
	return {
		daemonUrl:
			typeof parsed.daemonUrl === "string" && parsed.daemonUrl.length > 0
				? parsed.daemonUrl
				: defaults.daemonUrl,
		roles: {
			planner: role(roles.planner, defaults.roles.planner),
			implementer: role(roles.implementer, defaults.roles.implementer),
			reviewer: role(roles.reviewer, defaults.roles.reviewer),
		},
		limits: {
			defaultMaxAttempts: positiveNumber(
				limits.defaultMaxAttempts,
				defaults.limits.defaultMaxAttempts,
			),
			defaultBudgetMs: positiveNumber(
				limits.defaultBudgetMs,
				defaults.limits.defaultBudgetMs,
			),
			buildTimeoutMs: positiveNumber(
				limits.buildTimeoutMs,
				defaults.limits.buildTimeoutMs,
			),
			testTimeoutMs: positiveNumber(
				limits.testTimeoutMs,
				defaults.limits.testTimeoutMs,
			),
			preflightTimeoutMs: positiveNumber(
				limits.preflightTimeoutMs,
				defaults.limits.preflightTimeoutMs,
			),
			maxTasksPerRun: positiveNumber(
				limits.maxTasksPerRun,
				defaults.limits.maxTasksPerRun,
			),
		},
		commands: {
			preflight: command(commands.preflight, defaults.commands.preflight),
			build: command(commands.build, defaults.commands.build),
			test: command(commands.test, defaults.commands.test),
		},
		useWorktrees:
			typeof parsed.useWorktrees === "boolean"
				? parsed.useWorktrees
				: defaults.useWorktrees,
	};
}
