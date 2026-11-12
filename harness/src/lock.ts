import {
	existsSync,
	linkSync,
	mkdirSync,
	readFileSync,
	renameSync,
	statSync,
	unlinkSync,
	writeFileSync,
} from "node:fs";
import { dirname } from "node:path";

interface LockRecord {
	owner: string;
	pid: number;
	at: string;
	/** `ps -o lstart` of `pid`; a different value means the PID was reused. */
	started?: string;
	/** Process group of the running UE build/test child (detached, so pgid = child pid). */
	pgid?: number;
}

const psText = (args: string[]): string | undefined => {
	const ps = Bun.spawnSync(["ps", ...args]);
	return ps.exitCode === 0 ? new TextDecoder().decode(ps.stdout).trim() : undefined;
};

export const startTime = (pid: number): string | undefined =>
	psText(["-o", "lstart=", "-p", String(pid)]);

/** Live = exists, not a zombie, and (when recorded) the same process instance. */
const alive = (pid: number, started?: string): boolean => {
	try {
		process.kill(pid, 0);
	} catch (error) {
		if ((error as NodeJS.ErrnoException).code !== "EPERM") return false;
	}
	const stat = psText(["-o", "stat=", "-p", String(pid)]);
	if (!stat || stat.startsWith("Z")) return false;
	return started === undefined || startTime(pid) === started;
};

/** Any non-zombie member of the group: a UE build outliving a crashed harness still holds the lock. */
const groupActive = (pgid: number): boolean =>
	(psText(["-A", "-o", "pgid=,stat="]) ?? "")
		.split("\n")
		.some((line) => {
			const [group, stat] = line.trim().split(/\s+/);
			return Number(group) === pgid && !stat?.startsWith("Z");
		});

const held = (record: LockRecord): boolean =>
	alive(record.pid, record.started) ||
	(record.pgid !== undefined && groupActive(record.pgid));

export class BuildLock {
	constructor(private readonly path: string) {}

	tryAcquire(owner: string): boolean {
		mkdirSync(dirname(this.path), { recursive: true });
		const tmp = `${this.path}.${process.pid}.tmp`;
		const started = startTime(process.pid);
		const record = JSON.stringify({
			owner,
			pid: process.pid,
			at: new Date().toISOString(),
			...(started ? { started } : {}),
		} satisfies LockRecord);
		try {
			writeFileSync(tmp, record, { flag: "wx" });
			try {
				linkSync(tmp, this.path);
				unlinkSync(tmp);
				return true;
			} catch (error) {
				try {
					unlinkSync(tmp);
				} catch {
					/* best effort */
				}
				if ((error as NodeJS.ErrnoException).code !== "EEXIST")
					return false;
			}
		} catch {
			return false;
		}
		const current = this.holder();
		if (current && held(current)) return false;
		if (!current) {
			try {
				if (Date.now() - statSync(this.path).mtimeMs < 10 * 60_000)
					return false;
			} catch {
				return false;
			}
		}
		try {
			unlinkSync(this.path);
			return this.tryAcquire(owner);
		} catch {
			return false;
		}
	}

	/** Record the build child's process group so a harness crash cannot free the lock mid-build. */
	attachChild(owner: string, pgid: number): void {
		const current = this.holder();
		if (current?.owner !== owner) return;
		const tmp = `${this.path}.${process.pid}.child.tmp`;
		writeFileSync(tmp, JSON.stringify({ ...current, pgid }));
		renameSync(tmp, this.path);
	}

	release(owner: string): void {
		const current = this.holder();
		if (current?.owner !== owner) return;
		if (current.pgid !== undefined && groupActive(current.pgid)) {
			// Build leader exited but group members still run: hand the lock to the group only.
			const tmp = `${this.path}.${process.pid}.release.tmp`;
			writeFileSync(tmp, JSON.stringify({ ...current, pid: current.pgid, started: "released" }));
			renameSync(tmp, this.path);
			return;
		}
		try {
			unlinkSync(this.path);
		} catch {
			/* already released */
		}
	}

	holder(): LockRecord | null {
		if (!existsSync(this.path)) return null;
		try {
			const value = JSON.parse(
				readFileSync(this.path, "utf8"),
			) as Partial<LockRecord>;
			if (
				typeof value.owner !== "string" ||
				typeof value.pid !== "number" ||
				typeof value.at !== "string" ||
				(value.started !== undefined && typeof value.started !== "string") ||
				(value.pgid !== undefined && typeof value.pgid !== "number")
			)
				return null;
			return value as LockRecord;
		} catch {
			return null;
		}
	}
}

export async function withBuildLock<T>(
	lock: BuildLock,
	owner: string,
	work: () => T | Promise<T>,
): Promise<T | undefined> {
	if (!lock.tryAcquire(owner)) return undefined;
	try {
		return await work();
	} finally {
		lock.release(owner);
	}
}
