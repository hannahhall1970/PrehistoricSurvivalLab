import type { TaskState } from "./types.ts";

export interface SelectOptions {
	maxParallel?: number;
	ueSlots?: number;
}

export interface Selection {
	start: string[];
	waiting: Record<string, string>;
}

const DEAD: Record<string, true> = { FAILED: true, SKIPPED: true, BLOCKED: true };
const GLOB = /[*?[{]/;

// Static directory segments of an entry; a glob contributes only the
// segments before its first wildcard segment (empty = could match anything).
// ponytail: conservative prefix heuristic; `a/*/x` and `a/**/y` both cover all
// of `a/`, so disjoint globs under one prefix are serialized. Upgrade: real
// glob-intersection (e.g. picomatch on sampled paths) if it throttles real plans.
function base(entry: string): string[] {
	const segs = entry.replace(/^\.\//, "").split("/").filter(Boolean);
	const i = segs.findIndex((s) => GLOB.test(s));
	return i < 0 ? segs : segs.slice(0, i);
}

function entriesOverlap(a: string[], b: string[]): boolean {
	const n = Math.min(a.length, b.length);
	for (let i = 0; i < n; i++) if (a[i] !== b[i]) return false;
	return true;
}

// Empty `files` conflicts with everything.
function filesOverlap(a: string[], b: string[]): boolean {
	if (a.length === 0 || b.length === 0) return true;
	const bb = b.map(base);
	return a.some((x) => {
		const ab = base(x);
		return bb.some((y) => entriesOverlap(ab, y));
	});
}

export function selectRunnable(
	tasks: TaskState[],
	opts: SelectOptions = {},
): Selection {
	const maxParallel = opts.maxParallel ?? 3;
	const ueSlots = opts.ueSlots ?? 1;
	const byId = new Map(tasks.map((t) => [t.id, t]));
	const occupied = tasks.filter((t) => t.status === "RUNNING");
	let capacity = maxParallel - occupied.length;
	let ueUsed = occupied.filter((t) => t.needsBuild).length;
	const start: string[] = [];
	const waiting: Record<string, string> = {};

	for (const t of tasks) {
		if (t.status !== "PENDING") continue;
		const unmet = t.dependsOn.filter((d) => byId.get(d)?.status !== "PASSED");
		if (unmet.length) {
			const dead = unmet.find((d) => DEAD[byId.get(d)?.status ?? ""]);
			waiting[t.id] = dead
				? `dep-${byId.get(dead)?.status.toLowerCase()}:${dead}`
				: `deps:${unmet.join(",")}`;
			continue;
		}
		const clash = occupied.find((o) => filesOverlap(t.files, o.files));
		if (clash) waiting[t.id] = `file-conflict:${clash.id}`;
		else if (t.needsBuild && ueUsed >= ueSlots) waiting[t.id] = "ue-slot";
		else if (capacity <= 0) waiting[t.id] = "capacity";
		else {
			start.push(t.id);
			occupied.push(t);
			capacity--;
			if (t.needsBuild) ueUsed++;
		}
	}
	return { start, waiting };
}
