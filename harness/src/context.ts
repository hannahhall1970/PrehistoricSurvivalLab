import {
	existsSync,
	lstatSync,
	mkdirSync,
	readdirSync,
	readFileSync,
	realpathSync,
	writeFileSync,
} from "node:fs";
import { isAbsolute, join, sep } from "node:path";
import type { Failure, Role, RunState, TaskState } from "./types.ts";
import { WorktreeValidationError, validateWorktree } from "./worktree.ts";

export interface ContextBuilder {
	forPlanner(run: RunState): Promise<string>;
	forTask(
		run: RunState,
		task: TaskState,
		role: Role,
		evidence?: string[],
	): Promise<string>;
	writeHandoff(
		run: RunState,
		task: TaskState,
		data: {
			role: Role;
			summary: string;
			failure?: Failure;
			agentId?: string;
			evidence?: string[];
		},
	): string;
}

const bytes = (value: string): number => Buffer.byteLength(value, "utf8");
const globRegex = (pattern: string): RegExp => {
	let out = "^";
	for (let i = 0; i < pattern.length; i++) {
		const ch = pattern[i];
		if (ch === "*" && pattern[i + 1] === "*") {
			if (pattern[i + 2] === "/") {
				out += "(?:.*/)?";
				i += 2;
			} else {
				out += ".*";
				i++;
			}
		} else if (ch === "*") out += "[^/]*";
		else if (ch === "?") out += "[^/]";
		else out += /[\\^$+.()|{}[\]]/.test(ch ?? "") ? `\\${ch}` : ch;
	}
	return new RegExp(`${out}$`);
};
const sensitivePathPatterns = [
	/(?:^|\/)\.env(?:\..*)?$/i,
	/(?:^|\/)[^/]+\.(?:pem|key|p12|pfx|keystore|mobileprovision|p8)$/i,
	/(?:^|\/)(?:id_rsa|id_ed25519)[^/]*$/i,
	/(?:^|\/)(?:\.npmrc|\.netrc|\.pypirc|credentials[^/]*|secrets[^/]*)(?:\/|$)/i,
	/(?:^|\/)[^/]*\.secret[^/]*$/i,
	/(?:^|\/)(?:\.git|\.harness|\.ssh|\.aws|\.gnupg|private|Saved|Intermediate|DerivedDataCache|Binaries)(?:\/|$)/i,
];

export const redactSecrets = (text: string): string =>
	text
		.replace(
			/-----BEGIN [^\r\n]*?PRIVATE KEY-----[\s\S]*?-----END [^\r\n]*?PRIVATE KEY-----/gi,
			"[REDACTED PRIVATE KEY]",
		)
		.replace(/\bBearer\s+\S+/gi, "Bearer [REDACTED]")
		.replace(
			/((?:securitytoken|api[_-]?key|secret|token|password|passwd|auth|credential)[\w.-]*["']?\s*[:=]\s*)(?:"[^"]*"|'[^']*'|\S+)/gi,
			"$1[REDACTED]",
		)
		.replace(/gh[pousr]_[A-Za-z0-9]{20,}/g, "[REDACTED]")
		.replace(/github_pat_[A-Za-z0-9_]{20,}/g, "[REDACTED]")
		.replace(/sk-[A-Za-z0-9_-]{20,}/g, "[REDACTED]")
		.replace(/AKIA[0-9A-Z]{16}/g, "[REDACTED]")
		.replace(/xox[baprs]-\S+/g, "[REDACTED]");

const command = (cwd: string, args: string[]): string => {
	try {
		const result = Bun.spawnSync(args, { cwd, stdout: "pipe", stderr: "pipe" });
		return (
			new TextDecoder().decode(result.stdout) +
			new TextDecoder().decode(result.stderr)
		);
	} catch {
		return "(unavailable)";
	}
};

export class RepoContextBuilder implements ContextBuilder {
	private readonly maxFileBytes: number;
	private readonly maxTotalBytes: number;
	constructor(
		private readonly opts: {
			repoRoot: string;
			artifactDir: string;
			maxFileBytes?: number;
			maxTotalBytes?: number;
		},
	) {
		this.maxFileBytes = opts.maxFileBytes ?? 24_000;
		this.maxTotalBytes = opts.maxTotalBytes ?? 100_000;
	}

	async forPlanner(run: RunState): Promise<string> {
		const sections = [
			`# Planner context\nObjective: ${run.objective}\nRun: ${run.id}\nStage: ${run.stage}`,
			this.agents(),
			this.gitState(),
		];
		return this.limit(sections);
	}

	async forTask(
		run: RunState,
		task: TaskState,
		role: Role,
		evidence: string[] = [],
	): Promise<string> {
		let cwd = this.opts.repoRoot;
		if (run.worktreePath !== undefined) {
			const validation = validateWorktree(
				this.opts.repoRoot,
				run.id,
				run.worktreePath,
				run.worktreeBranch,
			);
			if (!validation.ok) throw new WorktreeValidationError(validation.reason);
			cwd = validation.path;
		}
		const sections = [
			`# ${role} context\nObjective: ${run.objective}\nRun: ${run.id}\nStage: ${run.stage}`,
			this.agents(),
			`## Task contract\n${JSON.stringify({ id: task.id, title: task.title, description: task.description, acceptance: task.acceptance, dependsOn: task.dependsOn, files: task.files, needsBuild: task.needsBuild, maxAttempts: task.maxAttempts, budgetMs: task.budgetMs }, null, 2)}`,
			this.gitState(cwd),
			// Reviewers get the change and the deterministic evidence inline; their tools are read-only.
			...(role === "reviewer"
				? [
						`## Git diff\n${command(cwd, ["git", "diff", "HEAD", "--no-color", "--no-ext-diff"])}`,
						`## Gate evidence (this attempt)\n${evidence.join("\n") || "(none recorded)"}`,
					]
				: []),
			this.taskFiles(task.files, cwd),
			this.previousFailure(task),
			this.previousHandoff(run, task),
			this.instructions(role),
		];
		return this.limit(sections);
	}

	writeHandoff(
		run: RunState,
		task: TaskState,
		data: {
			role: Role;
			summary: string;
			failure?: Failure;
			agentId?: string;
			evidence?: string[];
		},
	): string {
		const dir = join(this.opts.artifactDir, "runs", run.id, task.id);
		mkdirSync(dir, { recursive: true });
		const path = join(dir, `handoff-${task.attempt}-${data.role}.md`);
		const lines = [
			`# ${data.role} handoff`,
			`run: ${run.id}`,
			`task: ${task.id}`,
			`attempt: ${task.attempt}`,
			`summary: ${data.summary}`,
		];
		if (data.agentId) lines.push(`agentId: ${data.agentId}`);
		if (data.failure) lines.push(`failure: ${JSON.stringify(data.failure)}`);
		if (data.evidence?.length) lines.push("evidence:", ...data.evidence);
		writeFileSync(path, `${lines.join("\n")}\n`);
		return path;
	}

	private agents(): string {
		const path = join(this.opts.repoRoot, "AGENTS.md");
		return existsSync(path)
			? `## AGENTS.md\n${readFileSync(path, "utf8")}`
			: "## AGENTS.md\n(absent)";
	}

	private gitState(cwd = this.opts.repoRoot): string {
		return `## Git status\n${command(cwd, ["git", "status", "--short"])}## Git diff stat\n${command(cwd, ["git", "diff", "--stat"])}`;
	}

	private taskFiles(patterns: string[], cwd = this.opts.repoRoot): string {
		let output: string;
		try {
			const result = Bun.spawnSync(
				["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard"],
				{ cwd, stdout: "pipe", stderr: "pipe" },
			);
			if (result.exitCode !== 0)
				return "## Task files\n(unavailable: git ls-files failed)";
			output = new TextDecoder().decode(result.stdout);
		} catch {
			return "## Task files\n(unavailable: git ls-files failed)";
		}
		const all = output
			.split("\0")
			.filter(Boolean)
			.map((path) => path.replaceAll("\\", "/"));
		const matches = all.filter((path) =>
			patterns.some((pattern) =>
				globRegex(pattern.replaceAll("\\", "/")).test(path),
			),
		);
		let repoRoot: string;
		try {
			repoRoot = realpathSync(cwd);
		} catch {
			return "## Task files\n(unavailable: git ls-files failed)";
		}
		const sections: string[] = [];
		for (const path of matches) {
			if (sensitivePathPatterns.some((pattern) => pattern.test(path))) {
				sections.push(`### ${path}\n(excluded: sensitive path)`);
				continue;
			}
			if (
				isAbsolute(path) ||
				/^[A-Za-z]:[\\/]/.test(path) ||
				path.split(/[\\/]/).includes("..")
			)
				continue;
			const absolute = join(cwd, path);
			let content: string;
			try {
				const stat = lstatSync(absolute);
				if (!stat.isFile() || stat.isSymbolicLink()) continue;
				const resolved = realpathSync(absolute);
				if (resolved !== repoRoot && !resolved.startsWith(`${repoRoot}${sep}`))
					continue;
				content = readFileSync(resolved, "utf8");
			} catch {
				continue;
			}
			const safeContent = redactSecrets(content);
			const raw = Buffer.from(safeContent, "utf8");
			const clipped =
				raw.byteLength > this.maxFileBytes
					? `${raw.subarray(0, this.maxFileBytes).toString("utf8")}\n[truncated]`
					: safeContent;
			sections.push(`### ${path}\n${clipped}`);
			if (/^Source\/.*\/Public\/[^/]+\.h$/i.test(path)) {
				const signatures = content
					.split(/\r?\n/)
					.filter((line) =>
						/\b(?:UCLASS|UFUNCTION|UPROPERTY)\b|\b(?:class|struct|enum)\s+\w+|\b\w+\s*\([^;{}]*\)\s*(?:const\s*)?;/i.test(
							line,
						),
					);
				if (signatures.length)
					sections.push(`### ${path} signatures\n${signatures.join("\n")}`);
			}
		}
		return `## Task files\n${sections.join("\n") || "(none matched)"}`;
	}

	private previousFailure(task: TaskState): string {
		if (!task.lastFailure) return "## Previous failure\n(none)";
		return `## Previous failure\n${JSON.stringify({ ...task.lastFailure, excerpt: task.lastFailure.excerpt?.split(/\r?\n/).slice(0, 60).join("\n") }, null, 2)}`;
	}

	private previousHandoff(run: RunState, task: TaskState): string {
		const dir = join(this.opts.artifactDir, "runs", run.id, task.id);
		let realDir: string;
		try {
			const dirStat = lstatSync(dir);
			if (!dirStat.isDirectory() || dirStat.isSymbolicLink())
				return "## Previous handoff\n(none)";
			realDir = realpathSync(dir);
		} catch {
			return "## Previous handoff\n(none)";
		}
		const files = readdirSync(dir)
			.filter((name) => name.startsWith("handoff-") && name.endsWith(".md"))
			.sort();
		for (let index = files.length - 1; index >= 0; index--) {
			const latest = files[index];
			if (!latest) continue;
			const candidate = join(dir, latest);
			try {
				const stat = lstatSync(candidate);
				if (!stat.isFile() || stat.isSymbolicLink()) continue;
				const resolved = realpathSync(candidate);
				if (resolved !== realDir && !resolved.startsWith(`${realDir}${sep}`))
					continue;
				return `## Previous handoff\n${readFileSync(resolved, "utf8")}`;
			} catch {}
		}
		return "## Previous handoff\n(none)";
	}

	private instructions(role: Role): string {
		if (role === "implementer")
			return "## Instructions\nEdit only the allowed task files. Never run a full UE build; the harness runs deterministic gates. End with JSON summary.";
		if (role === "reviewer")
			return "## Instructions\nIndependent review of the Git diff above against the acceptance criteria and gate evidence. Your write/shell tools are disabled; use read/grep for more context. End with ReviewVerdict JSON (approve/reject and findings).";
		return "## Instructions\nProduce PlanOutput JSON with validated tasks, acceptance criteria, dependencies, files, and budgets.";
	}

	private limit(sections: string[]): string {
		let total = 0;
		const result: string[] = [];
		for (const section of sections) {
			if (!section) continue;
			const separator = result.length ? "\n\n" : "";
			const remaining = this.maxTotalBytes - total - bytes(separator);
			if (remaining <= 0) break;
			const safeSection = redactSecrets(section);
			const encoded = Buffer.from(safeSection, "utf8");
			let value: string;
			if (encoded.byteLength <= remaining) value = safeSection;
			else {
				const marker = "\n[context truncated]";
				const markerSize = bytes(marker);
				value =
					remaining <= markerSize
						? encoded.subarray(0, remaining).toString("utf8")
						: `${encoded.subarray(0, remaining - markerSize).toString("utf8")}${marker}`;
			}
			result.push(value);
			total += bytes(separator) + bytes(value);
			if (encoded.byteLength > remaining) break;
		}
		return result.join("\n\n");
	}
}
