import { createPaseoClient, type PaseoLogger } from "@getpaseo/client";
import { createHash } from "node:crypto";
import { lstatSync, readFileSync, realpathSync, statSync, writeFileSync } from "node:fs";
import { isAbsolute, join, resolve, sep } from "node:path";
import { configPath, loadConfig } from "./config.ts";
import type {
	AgentPort,
	AgentRequest,
	AgentResult,
	HarnessConfig,
	Role,
	RoleConfig,
} from "./types.ts";

type WaitResult = {
	status: AgentResult["status"];
	final: unknown;
	error: string | null;
	lastMessage: string | null;
};

type Permission = {
	id?: unknown;
	name?: unknown;
	title?: unknown;
	description?: unknown;
};

type AgentSnapshot = {
	id?: unknown;
	provider?: unknown;
	cwd?: unknown;
	model?: unknown;
	currentModeId?: unknown;
	thinkingOptionId?: unknown;
	effectiveThinkingOptionId?: unknown;
	workspaceId?: unknown;
	labels?: unknown;
	pendingPermissions?: unknown;
	status?: unknown;
	archivedAt?: unknown;
};

type TimelineEntry = {
	item?: unknown;
	stopReason?: unknown;
};

type AgentHandle = {
	id: string;
	workspaceId?: string | null;
	waitForFinish(timeoutMs?: number): Promise<WaitResult>;
	timeline?: {
		refetch(
			options?: Record<string, unknown>,
		): Promise<{ entries?: TimelineEntry[] }>;
	};
	pendingPermissions?: Permission[] | null;
	stop?: () => Promise<void>;
	cancel?: () => Promise<void>;
};

type ListAgentsOptions = {
	filter?: {
		labels?: Record<string, string>;
		includeArchived?: boolean;
	};
	page?: { limit: number };
};

type CreateAgentOptions = {
	idempotencyKey: string;
	config: {
		provider: string;
		modeId: string;
		thinkingOptionId?: string;
	};
	cwd: string;
	title: string;
	prompt: string;
	outputSchema?: Record<string, unknown>;
	env?: Record<string, string>;
	labels: Record<string, string>;
};

type PaseoClientLike = {
	connect?: () => Promise<void>;
	close?: () => Promise<void>;
	agents: {
		list: (options?: ListAgentsOptions) => Promise<{ entries?: unknown[] }>;
		ref: (agent: unknown | string) => AgentHandle;
		create: (options: CreateAgentOptions) => Promise<AgentHandle>;
	};
	providers?: {
		listAvailable?: () => Promise<unknown>;
		listModels: (provider: string) => Promise<unknown>;
	};
	cancelAgent?: (agentId: string) => Promise<void>;
	interruptAgent?: (agentId: string) => Promise<void>;
	stopAgent?: (agentId: string) => Promise<void>;
};

type Model = {
	id: string;
	thinkingOptions?: { id: string }[];
	defaultThinkingOptionId?: string;
};

export interface PaseoDiscoveryClient {
	providers: {
		listAvailable?: () => Promise<unknown>;
		listModels: (provider: string) => Promise<unknown>;
	};
}

export interface PaseoAgentPortOptions {
	config: HarnessConfig;
	logger?: PaseoLogger;
	/** Test seam; production callers omit this and use the configured daemon. */
	client?: unknown;
	/** Resolves repo-relative role overlays; defaults to this checkout. */
	repoRoot?: string;
}

export interface AgentRunHooks {
	onCreated?(agentId: string): void;
}

function isRecord(value: unknown): value is Record<string, unknown> {
	return typeof value === "object" && value !== null && !Array.isArray(value);
}

function stringValue(value: unknown): string | undefined {
	return typeof value === "string" && value.length > 0 ? value : undefined;
}

function errorMessage(error: unknown): string {
	return error instanceof Error ? error.message : String(error);
}

function sanitizeLabel(value: string): string {
	return value.replace(/[^A-Za-z0-9_.-]/g, "_").slice(0, 240);
}

export function harnessKeyLabel(idempotencyKey: string): string {
	return sanitizeLabel(idempotencyKey);
}

export function resolveRoleOverlay(
	repoRoot: string,
	overlay: string | undefined,
): string {
	const requested = typeof overlay === "string" ? overlay : "<missing>";
	const reject = (reason: string, cause?: unknown): never => {
		throw new Error(
			`Role config overlay rejected "${requested}": ${reason}`,
			cause === undefined ? undefined : { cause },
		);
	};
	if (typeof overlay !== "string") return reject("missing overlay");
	if (overlay.length === 0) return reject("empty path");

	const candidate = isAbsolute(overlay) ? overlay : resolve(repoRoot, overlay);
	let trustedRoot: string;
	try {
		trustedRoot = realpathSync(join(repoRoot, ".omp", "roles"));
	} catch (error) {
		return reject(
			`trusted roles root unavailable: ${errorMessage(error)}`,
			error,
		);
	}

	for (const dir of [join(repoRoot, ".omp"), join(repoRoot, ".omp", "roles")]) {
		try {
			if (lstatSync(dir).isSymbolicLink() || !lstatSync(dir).isDirectory())
				return reject(`trusted roles path is a symlink or not a directory: ${dir}`);
		} catch (error) {
			return reject(`trusted roles path unavailable: ${errorMessage(error)}`, error);
		}
	}

	let realPath: string;
	try {
		realPath = realpathSync(candidate);
	} catch (error) {
		const code =
			isRecord(error) && typeof error.code === "string" ? error.code : "";
		return reject(
			code === "ENOENT"
				? `file not found at ${candidate}`
				: `cannot resolve path at ${candidate}: ${errorMessage(error)}`,
			error,
		);
	}
	if (!realPath.startsWith(`${trustedRoot}${sep}`)) {
		return reject(
			`outside trusted root ${trustedRoot}; resolved to ${realPath}`,
		);
	}
	if (!realPath.endsWith(".yml")) {
		return reject(`must end in .yml; resolved to ${realPath}`);
	}
	try {
		if (!statSync(realPath).isFile()) {
			return reject(`not a regular file; resolved to ${realPath}`);
		}
	} catch (error) {
		return reject(
			`cannot inspect file ${realPath}: ${errorMessage(error)}`,
			error,
		);
	}
	return realPath;
}

type RoleOverlay = {
	path: string;
	fingerprint: string;
	env: Record<string, string>;
};

function overlayFingerprint(path: string, requested: string): string {
	try {
		return createHash("sha256")
			.update(path)
			.update("\0")
			.update(readFileSync(path))
			.digest("hex")
			.slice(0, 16);
	} catch (error) {
		throw new Error(
			`Role config overlay rejected "${requested}": file unreadable at ${path}: ${errorMessage(error)}`,
			{ cause: error },
		);
	}
}

type AgentExpectation = {
	provider: string;
	modeId: string;
	thinkingOptionId?: string;
	cwd: string;
	labels: Record<string, string>;
};

function snapshotFromEntry(entry: unknown): AgentSnapshot | undefined {
	if (!isRecord(entry)) return undefined;
	const candidate = isRecord(entry.agent) ? entry.agent : entry;
	return candidate;
}

function permissionFromSnapshot(
	snapshot: AgentSnapshot | undefined,
	handle: AgentHandle,
): AgentResult["permission"] {
	const snapshotPermissions = Array.isArray(snapshot?.pendingPermissions)
		? snapshot.pendingPermissions
		: [];
	const handlePermissions = Array.isArray(handle.pendingPermissions)
		? handle.pendingPermissions
		: [];
	const rawPermission = snapshotPermissions[0] ?? handlePermissions[0];
	if (!isRecord(rawPermission)) return undefined;
	const requestId = stringValue(rawPermission.id);
	if (!requestId) return undefined;
	return {
		requestId,
		description:
			stringValue(rawPermission.description) ??
			stringValue(rawPermission.title) ??
			stringValue(rawPermission.name) ??
			"Agent permission required",
	};
}

function agentIdFromSnapshot(
	snapshot: AgentSnapshot | undefined,
): string | undefined {
	return stringValue(snapshot?.id);
}

function labelsFromSnapshot(
	snapshot: AgentSnapshot | undefined,
): Record<string, string> {
	if (!isRecord(snapshot?.labels)) return {};
	return Object.fromEntries(
		Object.entries(snapshot.labels).filter(
			(entry): entry is [string, string] => typeof entry[1] === "string",
		),
	);
}

type JsonCandidate = { text: string; start: number; end: number };

function jsonCandidates(text: string): JsonCandidate[] {
	const candidates: JsonCandidate[] = [];
	const fenced = /```(?:json)?\s*([\s\S]*?)```/gi;
	for (const match of text.matchAll(fenced)) {
		if (match[1])
			candidates.push({
				text: match[1].trim(),
				start: match.index,
				end: match.index + match[0].length,
			});
	}
	candidates.push({ text: text.trim(), start: 0, end: text.length });
	for (let start = 0; start < text.length; start += 1) {
		if (text[start] !== "{" && text[start] !== "[") continue;
		let depth = 0;
		let quote = false;
		let escaped = false;
		for (let index = start; index < text.length; index += 1) {
			const character = text[index];
			if (quote) {
				if (escaped) escaped = false;
				else if (character === "\\") escaped = true;
				else if (character === '"') quote = false;
				continue;
			}
			if (character === '"') {
				quote = true;
				continue;
			}
			if (character === "{" || character === "[") depth += 1;
			else if (character === "}" || character === "]") depth -= 1;
			if (depth === 0) {
				candidates.push({ text: text.slice(start, index + 1), start, end: index + 1 });
				break;
			}
		}
	}
	return candidates;
}

/**
 * Agents are told to END their final message with the JSON object, so the candidate that ends
 * last wins (outermost first on ties). Taking the first parseable candidate let a JSON block
 * quoted earlier in the message (e.g. copied from an implementer's diff) override the real
 * trailing verdict.
 */
export function extractStructuredOutput(
	text: string | null | undefined,
): unknown {
	if (!text) return undefined;
	const ordered = jsonCandidates(text).sort(
		(a, b) => b.end - a.end || a.start - b.start,
	);
	for (const candidate of ordered) {
		try {
			return JSON.parse(candidate.text) as unknown;
		} catch (error) {
			if (!(error instanceof SyntaxError)) throw error;
		}
	}
	return undefined;
}

/**
 * Last assistant message plus its provider stop reason. @getpaseo/protocol 's timeline
 * items and agent snapshots declare no stop-reason field (checked AgentTimelineItem,
 * AgentTimelineEntryPayloadSchema, AgentSnapshotPayload), so a string `stopReason` on
 * the item/entry is read only if a daemon adds one. Error turns do expose it, embedded
 * in the error text as "(stopReason=X"; resultFromWait falls back to that.
 */
function assistantTail(entries: TimelineEntry[] | undefined): {
	text?: string;
	stopReason?: string;
} {
	if (!entries) return {};
	for (const entry of [...entries].reverse()) {
		if (!isRecord(entry.item)) continue;
		if (
			entry.item.type === "assistant_message" &&
			typeof entry.item.text === "string"
		) {
			const reason = entry.item.stopReason ?? entry.stopReason;
			return {
				text: entry.item.text,
				...(typeof reason === "string" ? { stopReason: reason } : {}),
			};
		}
	}
	return {};
}

function isConflict(error: unknown): boolean {
	return /agent_request_key_conflict|idempotency.*conflict|already exists/i.test(
		errorMessage(error),
	);
}

function modelFamily(id: string): string {
	const slash = id.indexOf("/");
	return slash === -1 ? id : id.slice(0, slash);
}

function chooseThinking(model: Model, preferred: string[]): string | undefined {
	const available = new Set(
		model.thinkingOptions?.map((option) => option.id) ?? [],
	);
	for (const option of preferred) {
		if (available.has(option)) return option;
	}
	return model.defaultThinkingOptionId ?? model.thinkingOptions?.[0]?.id;
}

function scoreModel(model: Model, role: Role): number {
	const id = model.id.toLowerCase();
	let score = 0;
	if (role === "planner") {
		if (id.includes("opus")) score += 1000;
		if (id.includes("gpt-6.1")) score += 900;
		if (id.includes("gpt-6")) score += 700;
		if (id.includes("sonnet-5.5")) score += 500;
	} else if (role === "implementer") {
		if (id.includes("gpt-6.1")) score += 1000;
		if (id.includes("gpt-6")) score += 800;
		if (id.includes("codex")) score += 650;
		if (id.includes("sonnet-5.5")) score += 550;
		if (id.includes("gpt-5.6")) score += 450;
	} else {
		if (id.includes("codex-auto-review")) score += 1000;
		if (id.includes("opus")) score += 800;
		if (id.includes("gpt-6")) score += 700;
		if (id.includes("sonnet-5.5")) score += 500;
	}
	if (id.includes("fast")) score -= 20;
	if (id.includes("haiku")) score -= 100;
	return score;
}

function chooseModel(
	models: Model[],
	role: Role,
	excludedFamilies: Set<string> = new Set(),
): Model {
	const preferred: Record<Role, string[]> = {
		planner: [
			"circle-ai/claude-opus-5-5",
			"circle-ai/claude-opus-5-5-high",
			"qrouter/claude-opus-5-5-high",
			"google-antigravity/claude-opus-4-6",
		],
		implementer: [
			"openai-codex/gpt-6.1-sol",
			"circle-ai/gpt-6.1-sol",
			"cliproxy/gpt-5.6-sol",
			"circle-ai/claude-sonnet-5-5",
		],
		reviewer: [
			"cliproxy/codex-auto-review",
			"circle-ai/codex-auto-review",
			"qrouter/codex-auto-review",
			"circle-ai/claude-opus-5-5",
		],
	};
	for (const id of preferred[role]) {
		const model = models.find(
			(candidate) =>
				candidate.id === id && !excludedFamilies.has(modelFamily(candidate.id)),
		);
		if (model) return model;
	}
	const choices = models.filter(
		(model) => !excludedFamilies.has(modelFamily(model.id)),
	);
	const fallbackChoices = choices.length > 0 ? choices : models;
	if (fallbackChoices.length === 0)
		throw new Error(`No usable OMP model found for ${role}`);
	return [...fallbackChoices].sort(
		(left, right) =>
			scoreModel(right, role) - scoreModel(left, role) ||
			left.id.localeCompare(right.id),
	)[0]!;
}

function roleConfig(
	model: Model,
	thinking: string[],
	previous?: RoleConfig,
): RoleConfig {
	const thinkingOptionId = chooseThinking(model, thinking);
	return {
		provider: `omp/${model.id}`,
		modeId: "full",
		...(thinkingOptionId ? { thinkingOptionId } : {}),
		...(previous?.configOverlay
			? { configOverlay: previous.configOverlay }
			: {}),
	};
}

function modelsFromResult(value: unknown): Model[] {
	if (!isRecord(value) || !Array.isArray(value.models)) return [];
	return value.models.filter(
		(model): model is Model => isRecord(model) && typeof model.id === "string",
	);
}

export async function discoverRoles(
	client: PaseoDiscoveryClient,
	repoRoot = resolve(import.meta.dir, "../.."),
): Promise<Record<Role, RoleConfig>> {
	const available = client.providers?.listAvailable
		? await client.providers.listAvailable()
		: undefined;
	if (isRecord(available) && Array.isArray(available.providers)) {
		const omp = available.providers.find(
			(provider) => isRecord(provider) && provider.provider === "omp",
		);
		if (isRecord(omp) && omp.available === false)
			throw new Error("Paseo provider omp is unavailable");
	}
	if (!client.providers?.listModels)
		throw new Error("Paseo client does not expose provider discovery");
	const models = modelsFromResult(await client.providers.listModels("omp"));
	if (models.length === 0) throw new Error("Paseo returned no OMP models");

	const planner = chooseModel(models, "planner");
	const implementer = chooseModel(models, "implementer");
	const reviewer = chooseModel(
		models,
		"reviewer",
		new Set([modelFamily(implementer.id)]),
	);
	const config = loadConfig(repoRoot);
	const thinking = ["high", "medium", "low", "minimal", "auto"];
	const roles: Record<Role, RoleConfig> = {
		planner: roleConfig(planner, thinking, config.roles.planner),
		implementer: roleConfig(implementer, thinking, config.roles.implementer),
		reviewer: roleConfig(reviewer, thinking, config.roles.reviewer),
	};
	config.roles = roles;
	writeFileSync(
		configPath(repoRoot),
		`${JSON.stringify(config, null, "\t")}\n`,
	);
	return roles;
}

export class PaseoAgentPort implements AgentPort {
	private readonly client: PaseoClientLike;
	private readonly config: HarnessConfig;
	private connected = false;
	private readonly logger?: PaseoLogger;

	private readonly repoRoot: string;

	constructor(options: PaseoAgentPortOptions) {
		this.config = options.config;
		this.logger = options.logger;
		this.repoRoot = options.repoRoot ?? resolve(import.meta.dir, "../..");
		this.client = (options.client ??
			createPaseoClient({
				url: options.config.daemonUrl,
				logger: options.logger,
			})) as unknown as PaseoClientLike;
	}

	/** Role tool policy travels as an OMP settings overlay; a missing file must not silently widen permissions. */
	private roleEnv(overlay?: string): RoleOverlay {
		const requested = typeof overlay === "string" ? overlay : "<missing>";
		const path = resolveRoleOverlay(this.repoRoot, overlay);
		return {
			path,
			fingerprint: overlayFingerprint(path, requested),
			env: { PI_CONFIG_FILES: path },
		};
	}

	private expectation(
		request: AgentRequest,
		role: RoleConfig,
		overlay: RoleOverlay,
	): AgentExpectation {
		const labels = {
			...request.labels,
			"harness.key": harnessKeyLabel(request.idempotencyKey),
			"harness.role": request.role,
			"harness.overlay": overlay.fingerprint,
			"harness.mode": role.modeId,
			"harness.provider": sanitizeLabel(role.provider),
		};
		return {
			provider: role.provider,
			modeId: role.modeId,
			...(role.thinkingOptionId
				? { thinkingOptionId: role.thinkingOptionId }
				: {}),
			cwd: request.cwd,
			labels,
		};
	}

	private incompatible(
		key: string,
		agentId: string,
		field: string,
		expected: unknown,
		got: unknown,
	): never {
		const expectedText =
			expected === undefined
				? "<missing>"
				: expected === null
					? "<null>"
					: String(expected);
		const gotText =
			got === undefined ? "<missing>" : got === null ? "<null>" : String(got);
		throw new Error(
			`agent ${agentId} for key ${key} is incompatible: ${field} expected ${expectedText} got ${gotText}`,
		);
	}

	private verifyExisting(
		request: AgentRequest,
		snapshot: AgentSnapshot,
		expected: AgentExpectation,
	): void {
		const agentId = agentIdFromSnapshot(snapshot) ?? "<missing>";
		if (snapshot.archivedAt !== undefined && snapshot.archivedAt !== null) {
			this.incompatible(
				request.idempotencyKey,
				agentId,
				"archived",
				"active",
				snapshot.archivedAt,
			);
		}

		const labels = labelsFromSnapshot(snapshot);
		for (const key of ["harness.overlay", "harness.mode", "harness.provider"]) {
			if (labels[key] !== expected.labels[key]) {
				this.incompatible(
					request.idempotencyKey,
					agentId,
					key,
					expected.labels[key],
					labels[key],
				);
			}
		}
		for (const key of [
			"harness.key",
			"harness.role",
			"harness.runId",
			"harness.taskId",
		]) {
			const expectedValue = expected.labels[key];
			if (expectedValue !== undefined && labels[key] !== expectedValue) {
				this.incompatible(
					request.idempotencyKey,
					agentId,
					key,
					expectedValue,
					labels[key],
				);
			}
		}

		const provider = stringValue(snapshot.provider);
		if (provider !== undefined && provider !== expected.provider) {
			this.incompatible(
				request.idempotencyKey,
				agentId,
				"provider",
				expected.provider,
				provider,
			);
		}
		const mode = stringValue(snapshot.currentModeId);
		if (mode !== undefined && mode !== expected.modeId) {
			this.incompatible(
				request.idempotencyKey,
				agentId,
				"mode",
				expected.modeId,
				mode,
			);
		}
		if (
			provider === undefined &&
			labels["harness.provider"] !== expected.labels["harness.provider"]
		) {
			this.incompatible(
				request.idempotencyKey,
				agentId,
				"provider",
				expected.provider,
				labels["harness.provider"],
			);
		}
		if (
			mode === undefined &&
			labels["harness.mode"] !== expected.labels["harness.mode"]
		) {
			this.incompatible(
				request.idempotencyKey,
				agentId,
				"mode",
				expected.modeId,
				labels["harness.mode"],
			);
		}

		const cwd = stringValue(snapshot.cwd);
		if (cwd !== expected.cwd) {
			this.incompatible(
				request.idempotencyKey,
				agentId,
				"cwd",
				expected.cwd,
				snapshot.cwd,
			);
		}
		const thinking =
			stringValue(snapshot.thinkingOptionId) ??
			stringValue(snapshot.effectiveThinkingOptionId);
		if (thinking !== undefined && thinking !== expected.thinkingOptionId) {
			this.incompatible(
				request.idempotencyKey,
				agentId,
				"thinkingOptionId",
				expected.thinkingOptionId,
				thinking,
			);
		}
	}

	private async connect(): Promise<void> {
		if (!this.connected && this.client.connect) {
			await this.client.connect();
			this.connected = true;
		}
	}

	private async findExisting(key: string): Promise<AgentSnapshot | undefined> {
		const listed = await this.client.agents.list({
			filter: {
				labels: { "harness.key": harnessKeyLabel(key) },
				includeArchived: true,
			},
			page: { limit: 100 },
		});
		for (const entry of listed.entries ?? []) {
			const snapshot = snapshotFromEntry(entry);
			if (labelsFromSnapshot(snapshot)["harness.key"] === harnessKeyLabel(key))
				return snapshot;
		}
		return undefined;
	}

	private async handleFor(
		request: AgentRequest,
		hooks?: AgentRunHooks,
	): Promise<AgentHandle> {
		const role = this.config.roles[request.role];
		if (!role) throw new Error(`Unknown agent role: ${request.role}`);
		const overlay = this.roleEnv(role.configOverlay);
		const expected = this.expectation(request, role, overlay);
		const existing = await this.findExisting(request.idempotencyKey);
		if (existing) {
			const existingId = agentIdFromSnapshot(existing);
			if (!existingId) {
				this.incompatible(
					request.idempotencyKey,
					"<missing>",
					"id",
					"present",
					undefined,
				);
			}
			this.verifyExisting(request, existing, expected);
			hooks?.onCreated?.(existingId);
			return this.client.agents.ref(existing);
		}

		try {
			const handle = await this.client.agents.create({
				idempotencyKey: request.idempotencyKey,
				config: {
					provider: role.provider,
					modeId: role.modeId,
					...(role.thinkingOptionId
						? { thinkingOptionId: role.thinkingOptionId }
						: {}),
				},
				cwd: request.cwd,
				title: request.title,
				prompt: request.prompt,
				...(request.outputSchema ? { outputSchema: request.outputSchema } : {}),
				env: overlay.env,
				labels: expected.labels,
			});
			hooks?.onCreated?.(handle.id);
			return handle;
		} catch (error) {
			if (!isConflict(error)) throw error;
			const raced = await this.findExisting(request.idempotencyKey);
			if (!raced) throw error;
			const racedId = agentIdFromSnapshot(raced);
			if (!racedId) {
				this.incompatible(
					request.idempotencyKey,
					"<missing>",
					"id",
					"present",
					undefined,
				);
			}
			this.verifyExisting(request, raced, expected);
			hooks?.onCreated?.(racedId);
			return this.client.agents.ref(raced);
		}
	}

	private async resultFromWait(
		request: AgentRequest,
		handle: AgentHandle,
		wait: WaitResult,
	): Promise<AgentResult> {
		const snapshot = isRecord(wait.final) ? wait.final : undefined;
		const tail = await this.timelineTail(handle);
		const text = wait.lastMessage ?? tail.text;
		// Error text is only trusted for an errored turn; a stale snapshot.lastError from an
		// earlier turn must not mark a successful idle result as a length stop (REV-3).
		const stopReason =
			tail.stopReason ??
			(wait.status === "error"
				? (wait.error ?? stringValue(snapshot?.lastError))?.match(
						/\(stopReason=([A-Za-z_-]+)/,
					)?.[1]
				: undefined);
		const workspaceId =
			stringValue(handle.workspaceId) ?? stringValue(snapshot?.workspaceId);
		return {
			status: wait.status,
			agentId: handle.id,
			...(workspaceId ? { workspaceId } : {}),
			...(request.outputSchema
				? { structured: extractStructuredOutput(text) }
				: {}),
			...(text ? { text } : {}),
			...(wait.error ? { error: wait.error } : {}),
			...(stopReason ? { stopReason } : {}),
			...(wait.status === "permission"
				? { permission: permissionFromSnapshot(snapshot, handle) }
				: {}),
		};
	}

	private async timelineTail(
		handle: AgentHandle,
	): Promise<{ text?: string; stopReason?: string }> {
		if (!handle.timeline) return {};
		try {
			const result = await handle.timeline.refetch({
				direction: "tail",
				limit: 50,
				projection: "canonical",
			});
			return assistantTail(result.entries);
		} catch (error) {
			this.logger?.debug(
				{ error: errorMessage(error) },
				"Paseo timeline refetch failed",
			);
			return {};
		}
	}

	async run(
		request: AgentRequest,
		signal?: AbortSignal,
		hooks?: AgentRunHooks,
	): Promise<AgentResult> {
		await this.connect();
		signal?.throwIfAborted();
		const handle = await this.handleFor(request, hooks);
		let onAbort: (() => void) | undefined;
		let stopPromise: Promise<void> | undefined;
		const wait = handle.waitForFinish(request.timeoutMs).then(
			(value) => ({ kind: "result" as const, value }),
			(error) => ({ kind: "error" as const, error }),
		);
		const aborted = signal
			? Promise.withResolvers<{ kind: "aborted" }>()
			: undefined;
		if (signal && aborted) {
			onAbort = () => {
				if (stopPromise) return;
				stopPromise = this.stop(handle.id).catch((error) => {
					this.logger?.warn(
						{ error: errorMessage(error), agentId: handle.id },
						"Paseo agent stop failed",
					);
				});
				void stopPromise.then(() => aborted.resolve({ kind: "aborted" }));
			};
			signal.addEventListener("abort", onAbort, { once: true });
			if (signal.aborted) onAbort();
		}
		try {
			const outcome = aborted
				? await Promise.race([wait, aborted.promise])
				: await wait;
			if (outcome.kind === "aborted") {
				return {
					status: "error",
					agentId: handle.id,
					error: "Agent run aborted",
				};
			}
			if (outcome.kind === "error") {
				return {
					status: "error",
					agentId: handle.id,
					error: errorMessage(outcome.error),
				};
			}
			return this.resultFromWait(request, handle, outcome.value);
		} finally {
			if (signal && onAbort) signal.removeEventListener("abort", onAbort);
			if (stopPromise) await stopPromise;
		}
	}

	async stop(agentId: string): Promise<void> {
		const process = Bun.spawn(["paseo", "stop", agentId], {
			stdout: "ignore",
			stderr: "pipe",
		});
		const exitCode = await process.exited;
		if (exitCode === 0) return;
		const stderr = await new Response(process.stderr).text();
		const detail = stderr.trim();
		throw new Error(
			`paseo stop exited with ${exitCode}${detail ? `: ${detail}` : ""}`,
		);
	}

	async close(): Promise<void> {
		if (this.client.close) await this.client.close();
		this.connected = false;
	}
}
