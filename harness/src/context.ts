import {
	existsSync,
	mkdirSync,
	readdirSync,
	readFileSync,
	writeFileSync,
} from "node:fs";
import { join, relative } from "node:path";
import type { Failure, Role, RunState, TaskState } from "./types.ts";

export interface ContextBuilder {
	forPlanner(run: RunState): Promise<string>;
	forTask(run: RunState, task: TaskState, role: Role): Promise<string>;
	writeHandoff(
		run: RunState,
		task: TaskState,
		data: { role: Role; summary: string; failure?: Failure; agentId?: string },
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

const walk = (base: string, directory: string, result: string[]): string[] => {
	for (const entry of readdirSync(directory, { withFileTypes: true })) {
		if (
			entry.name === ".git" ||
			entry.name === "node_modules" ||
			entry.name === ".harness"
		)
			continue;
		const path = join(directory, entry.name);
		if (entry.isDirectory()) walk(base, path, result);
		else result.push(relative(base, path).replaceAll("\\", "/"));
	}
	return result;
};

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

	async forTask(run: RunState, task: TaskState, role: Role): Promise<string> {
		const sections = [
			`# ${role} context\nObjective: ${run.objective}\nRun: ${run.id}\nStage: ${run.stage}`,
			this.agents(),
			`## Task contract\n${JSON.stringify({ id: task.id, title: task.title, description: task.description, acceptance: task.acceptance, dependsOn: task.dependsOn, files: task.files, needsBuild: task.needsBuild, maxAttempts: task.maxAttempts, budgetMs: task.budgetMs }, null, 2)}`,
			this.gitState(),
			this.taskFiles(task.files),
			this.previousFailure(task),
			this.previousHandoff(run, task),
			this.instructions(role),
		];
		return this.limit(sections);
	}

	writeHandoff(
		run: RunState,
		task: TaskState,
		data: { role: Role; summary: string; failure?: Failure; agentId?: string },
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
		writeFileSync(path, `${lines.join("\n")}\n`);
		return path;
	}

	private agents(): string {
		const path = join(this.opts.repoRoot, "AGENTS.md");
		return existsSync(path)
			? `## AGENTS.md\n${readFileSync(path, "utf8")}`
			: "## AGENTS.md\n(absent)";
	}

	private gitState(): string {
		return `## Git status\n${command(this.opts.repoRoot, ["git", "status", "--short"])}## Git diff stat\n${command(this.opts.repoRoot, ["git", "diff", "--stat"])}`;
	}

	private taskFiles(patterns: string[]): string {
		const all = walk(this.opts.repoRoot, this.opts.repoRoot, []);
		const matches = all.filter((path) =>
			patterns.some((pattern) =>
				globRegex(pattern.replaceAll("\\", "/")).test(path),
			),
		);
		const sections: string[] = [];
		for (const path of matches) {
			const absolute = join(this.opts.repoRoot, path);
			let content: string;
			try {
				content = readFileSync(absolute, "utf8");
			} catch {
				continue;
			}
			const raw = Buffer.from(content, "utf8");
			const clipped =
				raw.byteLength > this.maxFileBytes
					? `${raw.subarray(0, this.maxFileBytes).toString("utf8")}\n[truncated]`
					: content;
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
		if (!existsSync(dir)) return "## Previous handoff\n(none)";
		const files = readdirSync(dir)
			.filter((name) => name.startsWith("handoff-") && name.endsWith(".md"))
			.sort();
		const latest = files.at(-1);
		return latest
			? `## Previous handoff\n${readFileSync(join(dir, latest), "utf8")}`
			: "## Previous handoff\n(none)";
	}

	private instructions(role: Role): string {
		if (role === "implementer")
			return "## Instructions\nEdit only the allowed task files. Never run a full UE build; the harness runs deterministic gates. End with JSON summary.";
		if (role === "reviewer")
			return "## Instructions\nRead-only independent review. Do not edit files. End with ReviewVerdict JSON (approve/reject and findings).";
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
			const encoded = Buffer.from(section, "utf8");
			let value: string;
			if (encoded.byteLength <= remaining) value = section;
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
