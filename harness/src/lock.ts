import {
	existsSync,
	linkSync,
	mkdirSync,
	readFileSync,
	statSync,
	unlinkSync,
	writeFileSync,
} from "node:fs";
import { dirname } from "node:path";

interface LockRecord {
	owner: string;
	pid: number;
	at: string;
}

const alive = (pid: number): boolean => {
	try {
		process.kill(pid, 0);
		return true;
	} catch (error) {
		return (error as NodeJS.ErrnoException).code === "EPERM";
	}
};

export class BuildLock {
	constructor(private readonly path: string) {}

	tryAcquire(owner: string): boolean {
		mkdirSync(dirname(this.path), { recursive: true });
		const tmp = `${this.path}.${process.pid}.tmp`;
		const record = JSON.stringify({
			owner,
			pid: process.pid,
			at: new Date().toISOString(),
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
		if (current && alive(current.pid)) return false;
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

	release(owner: string): void {
		const current = this.holder();
		if (current?.owner !== owner) return;
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
				typeof value.at !== "string"
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
