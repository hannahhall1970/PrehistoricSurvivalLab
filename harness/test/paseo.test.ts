import { afterEach, describe, expect, it } from "bun:test";
import { mkdirSync, mkdtempSync, readFileSync, rmSync } from "node:fs";
import { join } from "node:path";
import { discoverRoles, PaseoAgentPort } from "../src/paseo.ts";
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
		planner: { provider: "omp/circle-ai/claude-opus-5-5", modeId: "full" },
		implementer: { provider: "omp/openai-codex/gpt-6.1-sol", modeId: "full" },
		reviewer: { provider: "omp/cliproxy/codex-auto-review", modeId: "full" },
	},
};

const requests: AgentRequest[] = [];
const createdHandles: FakeHandle[] = [];

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

function fakeClient(options: {
	list: () => unknown[];
	wait?: FakeHandle["waitForFinish"];
	assistantText?: string;
	stop?: () => Promise<void>;
}) {
	return {
		agents: {
			list: async () => ({
				entries: options.list().map((agent) => ({ agent })),
			}),
			ref: (agent: { id: string }) => {
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
				const agent = {
					id: `agent-${createdHandles.length + 1}`,
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
		).toEqual({
			harness: "test",
			"harness.key": "run_task_implementer_1",
		});
		expect(
			(requests[0] as unknown as { config: Record<string, unknown> }).config,
		).toMatchObject({
			provider: "omp/openai-codex/gpt-6.1-sol",
			modeId: "full",
		});
	});

	it("reuses an existing labelled agent instead of creating a duplicate", async () => {
		const existing = {
			id: "agent-existing",
			labels: { "harness.key": "run_task_implementer_1" },
		};
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
});
