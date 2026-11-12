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

	it("reclaims a lock whose holder is a zombie", async () => {
		const path = tempLock();
		mkdirSync(join(path, ".."), { recursive: true });
		// Child exits but is never reaped while the shell parent sleeps: a zombie.
		const parent = Bun.spawn(["sh", "-c", "sh -c 'exit 0' & echo $!; exec sleep 5"], {
			stdout: "pipe",
		});
		const reader = parent.stdout.getReader();
		const { value } = await reader.read();
		const pid = Number(new TextDecoder().decode(value).trim());
		await Bun.sleep(300);
		writeFileSync(
			path,
			JSON.stringify({ owner: "zombie", pid, at: new Date().toISOString() }),
		);
		try {
			expect(new BuildLock(path).tryAcquire("new")).toBe(true);
		} finally {
			parent.kill();
			rmSync(path.split("/").slice(0, -2).join("/"), {
				recursive: true,
				force: true,
			});
		}
	});

	it("keeps a lock held by a live process", () => {
		const path = tempLock();
		mkdirSync(join(path, ".."), { recursive: true });
		writeFileSync(
			path,
			JSON.stringify({ owner: "live", pid: process.pid, at: new Date().toISOString() }),
		);
		expect(new BuildLock(path).tryAcquire("new")).toBe(false);
		rmSync(path.split("/").slice(0, -2).join("/"), { recursive: true, force: true });
	});

	it("reclaims a lock whose PID was reused by a different process", () => {
		const path = tempLock();
		mkdirSync(join(path, ".."), { recursive: true });
		writeFileSync(
			path,
			JSON.stringify({
				owner: "old",
				pid: process.pid,
				at: new Date().toISOString(),
				started: "Mon Jan  1 00:00:00 2001",
			}),
		);
		expect(new BuildLock(path).tryAcquire("new")).toBe(true);
		rmSync(path.split("/").slice(0, -2).join("/"), { recursive: true, force: true });
	});

	it("keeps the lock while an orphaned build process group is still running", async () => {
		const path = tempLock();
		mkdirSync(join(path, ".."), { recursive: true });
		const build = Bun.spawn(["sleep", "5"], { detached: true });
		writeFileSync(
			path,
			JSON.stringify({ owner: "crashed", pid: 999_999, at: new Date().toISOString(), pgid: build.pid }),
		);
		try {
			expect(new BuildLock(path).tryAcquire("new")).toBe(false);
			build.kill("SIGKILL");
			await build.exited;
			expect(new BuildLock(path).tryAcquire("new")).toBe(true);
		} finally {
			build.kill("SIGKILL");
			rmSync(path.split("/").slice(0, -2).join("/"), { recursive: true, force: true });
		}
	});

	it("records the build child group on the held lock only for its owner", () => {
		const path = tempLock();
		const lock = new BuildLock(path);
		expect(lock.tryAcquire("me")).toBe(true);
		lock.attachChild("someone-else", 123);
		expect(lock.holder()?.pgid).toBeUndefined();
		lock.attachChild("me", 456);
		expect(lock.holder()).toMatchObject({ owner: "me", pid: process.pid, pgid: 456 });
		expect(lock.holder()?.started).toBeString();
		lock.release("me");
		rmSync(path.split("/").slice(0, -2).join("/"), { recursive: true, force: true });
	});

	it("does not free the lock on release while the build group still runs", async () => {
		const path = tempLock();
		const lock = new BuildLock(path);
		const straggler = Bun.spawn(["sleep", "5"], { detached: true });
		try {
			expect(lock.tryAcquire("me")).toBe(true);
			lock.attachChild("me", straggler.pid);
			lock.release("me");
			expect(lock.tryAcquire("other")).toBe(false);
			straggler.kill("SIGKILL");
			await straggler.exited;
			expect(lock.tryAcquire("other")).toBe(true);
		} finally {
			straggler.kill("SIGKILL");
			rmSync(path.split("/").slice(0, -2).join("/"), { recursive: true, force: true });
		}
	});
});
