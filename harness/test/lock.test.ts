import { describe, expect, it } from "bun:test";
import {
	existsSync,
	mkdirSync,
	mkdtempSync,
	readFileSync,
	rmSync,
	writeFileSync,
} from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { BuildLock, withBuildLock } from "../src/lock.ts";

const tempLock = (): string => {
	const dir = mkdtempSync(join(tmpdir(), "psl-lock-"));
	return join(dir, "nested", "build.lock");
};

describe("BuildLock", () => {
	it("acquires, reports, and releases only its own lock", async () => {
		const path = tempLock();
		const lock = new BuildLock(path);
		expect(lock.tryAcquire("one")).toBe(true);
		expect(lock.holder()?.owner).toBe("one");
		expect(new BuildLock(path).tryAcquire("two")).toBe(false);
		lock.release("two");
		expect(existsSync(path)).toBe(true);
		lock.release("one");
		expect(existsSync(path)).toBe(false);
		rmSync(path.split("/").slice(0, -2).join("/"), {
			recursive: true,
			force: true,
		});
	});

	it("reclaims a stale lock", () => {
		const path = tempLock();
		mkdirSync(join(path, ".."), { recursive: true });
		writeFileSync(
			path,
			JSON.stringify({
				owner: "dead",
				pid: 999999,
				at: new Date().toISOString(),
			}),
		);
		const lock = new BuildLock(path);
		expect(lock.tryAcquire("new")).toBe(true);
		expect(JSON.parse(readFileSync(path, "utf8")).owner).toBe("new");
		rmSync(path.split("/").slice(0, -2).join("/"), {
			recursive: true,
			force: true,
		});
	});
	it("treats malformed lock records as busy", () => {
		const path = tempLock();
		mkdirSync(join(path, ".."), { recursive: true });
		writeFileSync(path, "{not-json");
		expect(new BuildLock(path).tryAcquire("new")).toBe(false);
		rmSync(path.split("/").slice(0, -2).join("/"), {
			recursive: true,
			force: true,
		});
	});

	it("releases around async work", async () => {
		const path = tempLock();
		const lock = new BuildLock(path);
		await expect(withBuildLock(lock, "owner", async () => "ok")).resolves.toBe(
			"ok",
		);
		expect(lock.holder()).toBeNull();
	});
});
