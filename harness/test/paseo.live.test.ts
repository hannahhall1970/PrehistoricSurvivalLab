import { createPaseoClient } from "@getpaseo/client";
import { expect, it, setDefaultTimeout } from "bun:test";
import { loadConfig } from "../src/config.ts";
import { PaseoAgentPort } from "../src/paseo.ts";
import type { AgentRequest } from "../src/types.ts";

const live = process.env.HARNESS_LIVE === "1";

if (live) setDefaultTimeout(150_000);

(live ? it : it.skip)(
	"runs a structured agent through the live Paseo daemon and deduplicates its key",
	async () => {
		const repoRoot = "/Applications/PrehistoricSurvivalLab";
		const config = loadConfig(repoRoot);
		const client = createPaseoClient({ url: config.daemonUrl });
		await client.connect();
		try {
			const before = await client.agents.list({ page: { limit: 100 } });
			const idempotencyKey = `harness-live-${Date.now()}`;
			const request: AgentRequest = {
				role: "implementer",
				idempotencyKey,
				title: "harness live smoke",
				prompt: 'Reply with JSON {"ok":true}',
				cwd: repoRoot,
				outputSchema: {
					type: "object",
					properties: { ok: { type: "boolean" } },
					required: ["ok"],
				},
				timeoutMs: 120_000,
				labels: { harness: "smoke" },
			};
			const port = new PaseoAgentPort({ config, client });
			const first = await port.run(request);
			const second = await port.run(request);
			const after = await client.agents.list({
				filter: {
					labels: {
						"harness.key": idempotencyKey.replace(/[^A-Za-z0-9_.-]/g, "_"),
					},
					includeArchived: true,
				},
				page: { limit: 100 },
			});

			expect(before.entries.length).toBeGreaterThanOrEqual(0);
			expect(first.status).toBe("idle");
			expect(first.structured).toEqual({ ok: true });
			expect(second.agentId).toBe(first.agentId);
			expect(after.entries).toHaveLength(1);
			await client.agents.ref(first.agentId).archive();
		} finally {
			await client.close();
		}
	},
);
