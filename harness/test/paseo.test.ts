import { afterEach, describe, expect, it } from "bun:test";
import { createHash } from "node:crypto";
import {
	mkdirSync,
	mkdtempSync,
	readFileSync,
	realpathSync,
	rmSync,
	symlinkSync,
	writeFileSync,
} from "node:fs";
import { join, resolve } from "node:path";
import {
	discoverRoles,
	harnessKeyLabel,
	PaseoAgentPort,
	resolveRoleOverlay,
} from "../src/paseo.ts";
import { defaultConfig } from "../src/config.ts";
import type { AgentRequest, HarnessConfig } from "../src/types.ts";

type FakeHandle = {
	id: string;
	waitForFinish: (timeoutMs?: number) => Promise<{
		status: "idle" | "error" | "permission" | "timeout";
		final: Record<string, unknown> | null;
		error: string | null;
		lastMessage: string | null;
	}>;
	timeline: {
		refetch: () => Promise<{ entries: { item: Record<string, unknown> }[] }>;
	};
	stop?: () => Promise<void>;
};

const config: HarnessConfig = {
	...defaultConfig(),
	roles: {
		planner: {
			provider: "omp/circle-ai/claude-opus-5-5",
			modeId: "full",
			configOverlay: ".omp/roles/readonly.yml",
		},
		implementer: {
			provider: "omp/openai-codex/gpt-6.1-sol",
			modeId: "full",
			configOverlay: ".omp/roles/implementer.yml",
		},
		reviewer: {
			provider: "omp/cliproxy/codex-auto-review",
			modeId: "full",
			configOverlay: ".omp/roles/readonly.yml",
		},
	},
};

const requests: AgentRequest[] = [];
const createdHandles: FakeHandle[] = [];
const refCalls: string[] = [];

function requestFor(overrides: Partial<AgentRequest> = {}): AgentRequest {
	return {
		role: "implementer",
		idempotencyKey: "run:task:implementer:1",
		title: "fake task",
		prompt: "Reply with JSON",
		cwd: "/tmp/repo",
		timeoutMs: 10_000,
		labels: { harness: "test" },
		outputSchema: { type: "object", properties: { ok: { type: "boolean" } } },
		...overrides,
	};
}
const REPO_ROOT = resolve(import.meta.dir, "../..");

function overlayFingerprint(path: string): string {
	return createHash("sha256")
		.update(path)
		.update("\0")
		.update(readFileSync(path))
		.digest("hex")
		.slice(0, 16);
}

function providerLabel(provider: string): string {
	return provider.replace(/[^A-Za-z0-9_.-]/g, "_").slice(0, 240);
}

function snapshotFor(
	request: AgentRequest,
	overrides: Record<string, unknown> = {},
): Record<string, unknown> {
	const role = config.roles[request.role];
	const overlay = resolveRoleOverlay(REPO_ROOT, role.configOverlay);
	return {
		id: "agent-existing",
		provider: role.provider,
		cwd: request.cwd,
		currentModeId: role.modeId,
		archivedAt: null,
		labels: {
			...request.labels,
			"harness.key": harnessKeyLabel(request.idempotencyKey),
			"harness.role": request.role,
			"harness.overlay": overlayFingerprint(overlay),
			"harness.mode": role.modeId,
			"harness.provider": providerLabel(role.provider),
		},
		...overrides,
	};
}

function fakeClient(options: {
	list: () => unknown[];
	wait?: FakeHandle["waitForFinish"];
	assistantText?: string;
	stop?: () => Promise<void>;
	createError?: Error;
}) {
	return {
		agents: {
			list: async () => ({
				entries: options.list().map((agent) => ({ agent })),
			}),
			ref: (agent: { id: string }) => {
				refCalls.push(agent.id);
				const handle: FakeHandle = {
					id: agent.id,
					waitForFinish:
						options.wait ??
						(async () => ({
							status: "idle",
							final: { id: agent.id, pendingPermissions: [] },
							error: null,
							lastMessage: options.assistantText ?? '```json\n{"ok":true}\n```',
						})),
					timeline: {
						refetch: async () => ({
							entries: [
								{
									item: {
										type: "assistant_message",
										text: options.assistantText ?? '{"ok":true}',
									},
								},
							],
						}),
					},
					stop: options.stop,
				};
				return handle;
			},
			create: async (input: Record<string, unknown>) => {
				if (options.createError) throw options.createError;
				const inputConfig = input.config as Record<string, unknown>;
				const agent = {
					id: `agent-${createdHandles.length + 1}`,
					provider: inputConfig.provider,
					cwd: input.cwd,
					currentModeId: inputConfig.modeId,
					thinkingOptionId: inputConfig.thinkingOptionId ?? null,
					archivedAt: null,
					labels: input.labels,
				};
				requests.push(input as unknown as AgentRequest);
				const handle = fakeClient({
					...options,
					list: () => [agent],
				}).agents.ref(agent);
				createdHandles.push(handle);
				return handle;
			},
		},
	};
}

afterEach(() => {
	requests.length = 0;
	createdHandles.length = 0;
	refCalls.length = 0;
});

describe("PaseoAgentPort", () => {
	it("passes the SDK schema and extracts fenced JSON from the final message", async () => {
		const client = fakeClient({
			list: () => [],
			assistantText: '```json\n{"ok":true}\n```',
		});
		const port = new PaseoAgentPort({ config, client });

		let createdAgentId: string | undefined;
		const result = await port.run(requestFor(), undefined, {
			onCreated: (agentId) => {
				createdAgentId = agentId;
			},
		});

		expect(createdAgentId).toBe("agent-1");
		expect(result.structured).toEqual({ ok: true });
		expect(requests).toHaveLength(1);
		expect(requests[0]?.idempotencyKey).toBe("run:task:implementer:1");
		expect(
			(requests[0] as unknown as { labels: Record<string, string> }).labels,
		).toMatchObject({
			harness: "test",
			"harness.key": "run_task_implementer_1",
			"harness.role": "implementer",
			"harness.mode": "full",
			"harness.provider": "omp_openai-codex_gpt-6.1-sol",
			"harness.overlay": overlayFingerprint(
				resolveRoleOverlay(REPO_ROOT, config.roles.implementer.configOverlay),
			),
		});
		expect(
			(requests[0] as unknown as { config: Record<string, unknown> }).config,
		).toMatchObject({
			provider: "omp/openai-codex/gpt-6.1-sol",
			modeId: "full",
		});
	});

	it("reuses an existing labelled agent instead of creating a duplicate", async () => {
		const request = requestFor();
		const existing = snapshotFor(request);
		let present = false;
		const client = fakeClient({ list: () => (present ? [existing] : []) });
		const port = new PaseoAgentPort({ config, client });

		const created: string[] = [];
		await port.run(requestFor(), undefined, {
			onCreated: (agentId) => created.push(agentId),
		});
		present = true;
		const second = await port.run(requestFor(), undefined, {
			onCreated: (agentId) => created.push(agentId),
		});

		expect(second.agentId).toBe("agent-existing");
		expect(created).toEqual(["agent-1", "agent-existing"]);
		expect(requests).toHaveLength(1);
	});

	it("maps pending permissions from the final snapshot", async () => {
		const client = fakeClient({
			list: () => [],
			wait: async () => ({
				status: "permission",
				final: {
					id: "permission-agent",
					pendingPermissions: [
						{ id: "perm-1", name: "write", description: "Approve file write" },
					],
				},
				error: null,
				lastMessage: null,
			}),
		});
		const port = new PaseoAgentPort({ config, client });

		const result = await port.run(requestFor());

		expect(result.status).toBe("permission");
		expect(result.permission).toEqual({
			requestId: "perm-1",
			description: "Approve file write",
		});
	});
	it("returns timeout without stopping the still-running agent", async () => {
		let stopCalls = 0;
		const client = fakeClient({
			list: () => [],
			wait: async () => ({
				status: "timeout",
				final: null,
				error: "timed out",
				lastMessage: null,
			}),
			stop: async () => {
				stopCalls++;
			},
		});
		const port = new PaseoAgentPort({ config, client });

		const result = await port.run(requestFor());

		expect(result.status).toBe("timeout");
		expect(stopCalls).toBe(0);
	});
});

describe("resolveRoleOverlay", () => {
	it("accepts trusted absolute paths and symlinks that resolve inside the roles root", () => {
		const root = mkdtempSync(join("/tmp", "psl-overlay-trusted-"));
		const roles = join(root, ".omp", "roles");
		const roleFile = join(roles, "readonly.yml");
		const alias = join(roles, "alias.yml");
		try {
			mkdirSync(roles, { recursive: true });
			writeFileSync(roleFile, "tools: {}\n");
			symlinkSync(roleFile, alias);

			expect(resolveRoleOverlay(root, roleFile)).toBe(realpathSync(roleFile));
			expect(resolveRoleOverlay(root, ".omp/roles/alias.yml")).toBe(
				realpathSync(roleFile),
			);
		} finally {
			rmSync(root, { recursive: true, force: true });
		}
	});
	it("rejects a roles directory symlink to an external valid overlay directory", () => {
		const root = mkdtempSync(join("/tmp", "psl-overlay-symlink-root-"));
		const outside = mkdtempSync(join("/tmp", "psl-overlay-symlink-outside-"));
		const roles = join(root, ".omp", "roles");
		const externalRoles = join(outside, "roles");
		try {
			mkdirSync(join(root, ".omp"), { recursive: true });
			mkdirSync(externalRoles, { recursive: true });
			writeFileSync(join(externalRoles, "external.yml"), "tools: {}\n");
			symlinkSync(externalRoles, roles);

			expect(() =>
				resolveRoleOverlay(root, join(roles, "external.yml")),
			).toThrow(/trusted roles path is a symlink/);
		} finally {
			rmSync(root, { recursive: true, force: true });
			rmSync(outside, { recursive: true, force: true });
		}
	});

	it("rejects escaping, missing, empty, non-YAML, and non-file overlays", () => {
		const root = mkdtempSync(join("/tmp", "psl-overlay-rejected-"));
		const roles = join(root, ".omp", "roles");
		const outside = join(root, "outside.yml");
		try {
			mkdirSync(roles, { recursive: true });
			writeFileSync(outside, "tools: {}\n");
			writeFileSync(join(roles, "role.json"), "{}\n");
			mkdirSync(join(roles, "directory.yml"));
			symlinkSync(outside, join(roles, "escape.yml"));

			for (const overlay of [
				"../../etc/x.yml",
				".omp/roles/../../outside.yml",
				outside,
				join(roles, "escape.yml"),
				join(roles, "directory.yml"),
				join(roles, "missing.yml"),
				"",
				".omp/roles/role.json",
			]) {
				expect(() => resolveRoleOverlay(root, overlay)).toThrow(
					/Role config overlay rejected/,
				);
			}
		} finally {
			rmSync(root, { recursive: true, force: true });
		}
	});

	it("rejects a missing planner or reviewer overlay instead of widening permissions", async () => {
		const root = mkdtempSync(join("/tmp", "psl-overlay-role-"));
		try {
			const missingConfig: HarnessConfig = {
				...config,
				roles: {
					...config.roles,
					reviewer: { ...config.roles.reviewer, configOverlay: undefined },
				},
			};
			const port = new PaseoAgentPort({
				config: missingConfig,
				client: fakeClient({ list: () => [] }),
				repoRoot: root,
			});
			await expect(
				port.run(
					requestFor({ role: "reviewer", idempotencyKey: "reviewer-missing" }),
				),
			).rejects.toThrow(/overlay rejected.*missing/i);
		} finally {
			rmSync(root, { recursive: true, force: true });
		}
	});
});

describe("PaseoAgentPort existing-agent verification", () => {
	it("rejects an existing agent with a stale overlay fingerprint without ref", async () => {
		const request = requestFor({ idempotencyKey: "stale-overlay" });
		const existing = snapshotFor(request);
		(existing.labels as Record<string, string>)["harness.overlay"] =
			"deadbeefdeadbeef";
		const port = new PaseoAgentPort({
			config,
			client: fakeClient({ list: () => [existing] }),
		});

		await expect(port.run(request)).rejects.toThrow(
			/overlay expected .*got deadbeef/i,
		);
		expect(refCalls).toEqual([]);
	});

	it("rejects an existing agent with a different provider without ref", async () => {
		const request = requestFor({ idempotencyKey: "provider-mismatch" });
		const existing = snapshotFor(request, { provider: "omp/other/model" });
		const port = new PaseoAgentPort({
			config,
			client: fakeClient({ list: () => [existing] }),
		});

		await expect(port.run(request)).rejects.toThrow(
			/provider expected omp\/openai-codex\/gpt-6\.1-sol got omp\/other\/model/,
		);
		expect(refCalls).toEqual([]);
	});

	it("rejects an implementer-labelled agent for a reviewer key", async () => {
		const request = requestFor({
			role: "reviewer",
			idempotencyKey: "reviewer-key",
		});
		const existing = snapshotFor(request);
		(existing.labels as Record<string, string>)["harness.role"] = "implementer";
		const port = new PaseoAgentPort({
			config,
			client: fakeClient({ list: () => [existing] }),
		});

		await expect(port.run(request)).rejects.toThrow(
			/role expected reviewer got implementer/,
		);
		expect(refCalls).toEqual([]);
	});

	it("verifies the conflict-race snapshot before reusing it", async () => {
		const request = requestFor({ idempotencyKey: "conflict-race" });
		const raced = snapshotFor(request);
		(raced.labels as Record<string, string>)["harness.overlay"] =
			"badbadbadbadbadb";
		let listCalls = 0;
		const port = new PaseoAgentPort({
			config,
			client: fakeClient({
				list: () => (++listCalls > 1 ? [raced] : []),
				createError: new Error("agent_request_key_conflict"),
			}),
		});

		await expect(port.run(request)).rejects.toThrow(
			/overlay expected .*got badbadbadbadbadb/i,
		);
		expect(refCalls).toEqual([]);
		expect(requests).toHaveLength(0);
	});

	it("creates a fresh agent under a different idempotency key after rejection", async () => {
		const staleRequest = requestFor({ idempotencyKey: "stale-key" });
		const freshRequest = requestFor({ idempotencyKey: "fresh-key" });
		const stale = snapshotFor(staleRequest);
		(stale.labels as Record<string, string>)["harness.overlay"] =
			"deadbeefdeadbeef";
		let first = true;
		const port = new PaseoAgentPort({
			config,
			client: fakeClient({
				list: () => {
					if (first) {
						first = false;
						return [stale];
					}
					return [];
				},
			}),
		});

		await expect(port.run(staleRequest)).rejects.toThrow(/overlay expected/);
		const result = await port.run(freshRequest);

		expect(result.agentId).toBe("agent-1");
		expect(requests).toHaveLength(1);
		expect(
			(requests[0] as unknown as { labels: Record<string, string> }).labels[
				"harness.key"
			],
		).toBe("fresh-key");
	});

	it("fails closed when the overlay is deleted before reuse", async () => {
		const root = mkdtempSync(join("/tmp", "psl-overlay-deleted-"));
		const roles = join(root, ".omp", "roles");
		const overlay = join(roles, "implementer.yml");
		try {
			mkdirSync(roles, { recursive: true });
			writeFileSync(overlay, "tools: {}\n");
			const localConfig: HarnessConfig = {
				...config,
				roles: {
					...config.roles,
					implementer: {
						...config.roles.implementer,
						configOverlay: ".omp/roles/implementer.yml",
					},
				},
			};
			const request = requestFor({ idempotencyKey: "deleted-overlay" });
			const provider = localConfig.roles.implementer.provider;
			const existing = {
				id: "agent-deleted-overlay",
				provider,
				cwd: request.cwd,
				currentModeId: "full",
				labels: {
					...request.labels,
					"harness.key": harnessKeyLabel(request.idempotencyKey),
					"harness.role": "implementer",
					"harness.overlay": overlayFingerprint(overlay),
					"harness.mode": "full",
					"harness.provider": providerLabel(provider),
				},
			};
			rmSync(overlay);
			const port = new PaseoAgentPort({
				config: localConfig,
				client: fakeClient({ list: () => [existing] }),
				repoRoot: root,
			});

			await expect(port.run(request)).rejects.toThrow(/file not found/i);
			expect(refCalls).toEqual([]);
		} finally {
			rmSync(root, { recursive: true, force: true });
		}
	});
});

describe("discoverRoles", () => {
	it("selects available strong models and writes a valid role config", async () => {
		const root = mkdtempSync(join(process.cwd(), "paseo-discover-test-"));
		mkdirSync(join(root, "harness"));
		try {
			const client = {
				providers: {
					listAvailable: async () => ({
						providers: [{ provider: "omp", available: true }],
					}),
					listModels: async () => ({
						models: [
							{
								id: "circle-ai/claude-opus-5-5",
								thinkingOptions: [{ id: "high" }],
							},
							{
								id: "openai-codex/gpt-6.1-sol",
								thinkingOptions: [{ id: "high" }],
							},
							{
								id: "cliproxy/codex-auto-review",
								thinkingOptions: [{ id: "high" }],
							},
						],
					}),
				},
			};

			const roles = await discoverRoles(client, root);
			const written = JSON.parse(
				readFileSync(join(root, "harness", "harness.config.json"), "utf8"),
			) as HarnessConfig;

			expect(roles.planner.provider).toBe("omp/circle-ai/claude-opus-5-5");
			expect(roles.implementer.provider).toBe("omp/openai-codex/gpt-6.1-sol");
			expect(roles.reviewer.provider).toBe("omp/cliproxy/codex-auto-review");
			expect(written.roles).toEqual(roles);
		} finally {
			rmSync(root, { recursive: true, force: true });
		}
	});

	it("passes the role overlay as PI_CONFIG_FILES and refuses a missing overlay", async () => {
		const root = mkdtempSync(join("/tmp", "psl-overlay-"));
		const roles = join(root, ".omp", "roles");
		mkdirSync(roles, { recursive: true });
		const roleOverlay = join(roles, "ro.yml");
		writeFileSync(roleOverlay, "tools: {}\n");
		const withOverlay: HarnessConfig = {
			...config,
			roles: {
				...config.roles,
				reviewer: {
					...config.roles.reviewer,
					configOverlay: ".omp/roles/ro.yml",
				},
				implementer: {
					...config.roles.implementer,
					configOverlay: ".omp/roles/missing.yml",
				},
			},
		};
		try {
			const port = new PaseoAgentPort({
				config: withOverlay,
				client: fakeClient({ list: () => [] }),
				repoRoot: root,
			});
			await port.run(requestFor({ role: "reviewer", idempotencyKey: "r:1" }));
			expect(
				(requests[0] as unknown as { env?: Record<string, string> }).env,
			).toEqual({ PI_CONFIG_FILES: resolveRoleOverlay(root, roleOverlay) });
			await expect(
				port.run(requestFor({ role: "implementer", idempotencyKey: "i:1" })),
			).rejects.toThrow(/Role config overlay rejected.*file not found/i);
			expect(requests).toHaveLength(1);
		} finally {
			rmSync(root, { recursive: true, force: true });
		}
	});
});
