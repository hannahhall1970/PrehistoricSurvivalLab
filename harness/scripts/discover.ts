#!/usr/bin/env bun
import { createPaseoClient } from "@getpaseo/client";
import { resolve } from "node:path";
import { loadConfig } from "../src/config.ts";
import { discoverRoles } from "../src/paseo.ts";

const repoRoot = resolve(import.meta.dir, "../..");
const config = loadConfig(repoRoot);
const client = createPaseoClient({ url: config.daemonUrl });

try {
	await client.connect();
	const roles = await discoverRoles(client, repoRoot);
	console.log(JSON.stringify(roles, null, 2));
} finally {
	await client.close();
}
