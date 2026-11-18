// Blender -> Unreal asset manifest validator. Stdlib only.
// Usage: bun scripts/asset_manifest.ts validate <manifest.json> [--root <repoRoot>] [--json]
//        bun scripts/asset_manifest.ts hash <file>
import { createHash } from "node:crypto";
import { existsSync, readFileSync, statSync } from "node:fs";
import { isAbsolute, relative, resolve } from "node:path";

export const STAGES = [
	"blender_export",
	"fbx_lint",
	"ue_import",
	"ue_automation",
	"packaged_capture",
	"human_art_review",
] as const;
const STATUSES = ["pending", "passed", "failed"];
const ROLES = ["production", "experimental"];
const SHA = /^[0-9a-f]{64}$/;

type Obj = Record<string, unknown>;
export type Result = { valid: boolean; errors: string[]; promotable: Record<string, boolean> };

export function sha256(data: Buffer | string): string {
	return createHash("sha256").update(data).digest("hex");
}

// Git LFS pointer: the on-disk file is ~130 bytes of text, the real bytes live in LFS.
function lfsOid(buf: Buffer): string | null {
	if (buf.length > 512) return null;
	const m = /^version https:\/\/git-lfs\.github\.com\/spec\/v1\noid sha256:([0-9a-f]{64})\n/.exec(buf.toString("utf8"));
	return m ? m[1] : null;
}

const isObj = (v: unknown): v is Obj => typeof v === "object" && v !== null && !Array.isArray(v);
const nonEmpty = (v: unknown): v is string => typeof v === "string" && v.trim() !== "";

export function validateManifest(manifest: unknown, root: string): Result {
	const errors: string[] = [];
	const promotable: Record<string, boolean> = {};
	if (!isObj(manifest) || !Array.isArray(manifest.assets)) {
		return { valid: false, errors: ["manifest: expected object with an `assets` array"], promotable };
	}
	const seen = new Set<string>();
	const packages: { id: string; role: string; pkg: string }[] = [];

	for (const [i, a] of (manifest.assets as unknown[]).entries()) {
		const id = isObj(a) && nonEmpty(a.id) ? a.id : `#${i}`;
		const before = errors.length;
		const err = (field: string, msg: string) => errors.push(`${id}: ${field}: ${msg}`);
		if (!isObj(a)) {
			err("(asset)", "must be an object");
			continue;
		}
		if (!nonEmpty(a.id)) err("id", "missing");
		else if (seen.has(a.id)) err("id", "duplicate asset id");
		else seen.add(a.id);
		if (!nonEmpty(a.category)) err("category", "missing");
		if (!ROLES.includes(a.role as string)) err("role", `must be one of ${ROLES.join("|")}`);
		const production = a.role === "production";

		// evidence/source/export files: relative paths must stay inside root; absolute paths
		// are allowed for external art (e.g. ~/PSL-private-assets).
		const locate = (field: string, p: unknown): string | null => {
			if (!nonEmpty(p)) {
				err(field, "path missing");
				return null;
			}
			if (isAbsolute(p)) return p;
			const full = resolve(root, p);
			const rel = relative(resolve(root), full);
			if (rel === ".." || rel.startsWith("../") || isAbsolute(rel)) {
				err(field, `relative path escapes root: ${p}`);
				return null;
			}
			return full;
		};
		const checkFile = (field: string, f: unknown, needHash: boolean) => {
			if (!isObj(f)) return err(field, "missing object");
			const path = locate(`${field}.path`, f.path);
			if (f.sha256 !== undefined && !(typeof f.sha256 === "string" && SHA.test(f.sha256))) err(`${field}.sha256`, "must be 64 hex chars");
			else if (needHash && f.sha256 === undefined) err(`${field}.sha256`, "missing");
			if (!path || !existsSync(path)) {
				if (path && production) err(`${field}.path`, `file does not exist: ${f.path}`);
				return;
			}
			if (!statSync(path).isFile()) return err(`${field}.path`, `not a regular file: ${f.path}`);
			const buf = readFileSync(path);
			const oid = lfsOid(buf);
			if (oid) {
				// pointer: sha256 in manifest is the LFS oid (hash of the real content), which we can compare.
				if (f.sha256 !== undefined && f.sha256 !== oid) err(`${field}.sha256`, `mismatch: manifest ${f.sha256} vs LFS oid ${oid}`);
				if (production) err(`${field}.path`, `is a Git LFS pointer (real content not materialized): ${f.path}`);
			} else if (typeof f.sha256 === "string" && SHA.test(f.sha256) && sha256(buf) !== f.sha256) {
				err(`${field}.sha256`, `mismatch: manifest ${f.sha256} vs actual ${sha256(buf)}`);
			}
		};

		checkFile("source", a.source, production);
		if (isObj(a.source) && !nonEmpty(a.source.license_ref)) err("source.license_ref", "missing license/provenance ref");
		if (production || a.export !== undefined) checkFile("export", a.export, production);
		if (production && isObj(a.export)) {
			for (const k of ["exporter", "exporter_version", "unit_scale", "up_axis", "triangle_count", "bone_count", "lod_count"]) {
				if (a.export[k] === undefined || a.export[k] === null || a.export[k] === "") err(`export.${k}`, "required for production");
			}
		}

		const imp = a.import;
		if (!isObj(imp) || !nonEmpty(imp.package) || !String(imp.package).startsWith("/Game/")) {
			err("import.package", "must be a /Game/... package path");
		} else {
			packages.push({ id, role: String(a.role), pkg: String(imp.package).replace(/\/+$/, "") });
			if (production && !nonEmpty(imp.script)) err("import.script", "required for production");
			if (production && !nonEmpty(imp.skeleton)) err("import.skeleton", "required for production");
		}

		const stages = isObj(a.stages) ? a.stages : {};
		if (!isObj(a.stages)) err("stages", "missing");
		for (const s of STAGES) {
			const st = stages[s];
			if (!isObj(st)) {
				err(`stages.${s}`, "missing");
				continue;
			}
			if (!STATUSES.includes(st.status as string)) {
				err(`stages.${s}.status`, `must be one of ${STATUSES.join("|")}`);
				continue;
			}
			if (production && st.status !== "passed") err(`stages.${s}.status`, `production requires passed, got ${st.status}`);
			if (st.status === "passed" || production) {
				const p = locate(`stages.${s}.evidence`, st.evidence);
				if (p && !existsSync(p)) err(`stages.${s}.evidence`, `file does not exist: ${st.evidence}`);
				else if (p && !statSync(p).isFile()) err(`stages.${s}.evidence`, `not a file: ${st.evidence}`);
			}
			if (s === "ue_automation" && (production || st.status === "passed")) {
				if (!Array.isArray(st.tests) || !st.tests.length || !st.tests.every((t: unknown) => typeof t === "string" && t.startsWith("PSL."))) {
					err("stages.ue_automation.tests", "need a non-empty list of PSL.* test names");
				}
			}
			if (s === "human_art_review" && (production || st.status === "passed")) {
				if (!nonEmpty(st.reviewer)) err("stages.human_art_review.reviewer", "missing");
				if (typeof st.date !== "string" || !/^\d{4}-\d{2}-\d{2}$/.test(st.date)) err("stages.human_art_review.date", "must be YYYY-MM-DD");
				if (production && st.verdict !== "approve") err("stages.human_art_review.verdict", `production requires approve, got ${st.verdict ?? "none"}`);
			}
		}
		promotable[id] = production && errors.length === before;
	}

	// package collisions: exact match or nesting between a production package and any other asset.
	for (const [i, x] of packages.entries()) {
		for (const y of packages.slice(i + 1)) {
			if (x.role !== "production" && y.role !== "production") continue;
			if (!(x.pkg === y.pkg || x.pkg.startsWith(y.pkg + "/") || y.pkg.startsWith(x.pkg + "/"))) continue;
			const [prod, other] = x.role === "production" ? [x, y] : [y, x];
			const msg = `import.package: ${other.pkg} collides with production package ${prod.pkg} (owned by ${prod.id})`;
			errors.push(`${other.id}: ${msg}`);
			if (other.role === "production") errors.push(`${prod.id}: import.package: ${prod.pkg} collides with ${other.id}`);
			promotable[other.id] = promotable[prod.id] = false;
		}
	}
	return { valid: errors.length === 0, errors, promotable };
}

const USAGE = "usage: bun scripts/asset_manifest.ts validate <manifest.json> [--root <repoRoot>] [--json]\n       bun scripts/asset_manifest.ts hash <file>";

export function main(argv: string[]): number {
	const [cmd, ...rest] = argv;
	if (cmd === "hash") {
		if (rest.length !== 1) return (console.error(USAGE), 2);
		if (!existsSync(rest[0])) return (console.error(`no such file: ${rest[0]}`), 1);
		const buf = readFileSync(rest[0]);
		const oid = lfsOid(buf);
		console.log(`${sha256(buf)}  ${rest[0]}${oid ? `  (Git LFS pointer, oid ${oid})` : ""}`);
		return 0;
	}
	if (cmd !== "validate") return (console.error(USAGE), 2);
	let file: string | undefined;
	let root = process.cwd();
	let asJson = false;
	for (let i = 0; i < rest.length; i++) {
		if (rest[i] === "--json") asJson = true;
		else if (rest[i] === "--root") {
			if (!rest[i + 1]) return (console.error(USAGE), 2);
			root = resolve(rest[++i]);
		} else if (rest[i].startsWith("--") || file) return (console.error(USAGE), 2);
		else file = rest[i];
	}
	if (!file) return (console.error(USAGE), 2);

	let result: Result;
	try {
		result = validateManifest(JSON.parse(readFileSync(file, "utf8")), root);
	} catch (e) {
		result = { valid: false, errors: [`${file}: ${e instanceof Error ? e.message : String(e)}`], promotable: {} };
	}
	if (asJson) console.log(JSON.stringify(result, null, 2));
	else {
		for (const [id, ok] of Object.entries(result.promotable)) console.log(`${ok ? "PROMOTABLE" : "NOT PROMOTABLE"}: ${id}`);
		for (const e of result.errors) console.log(`ERROR ${e}`);
		console.log(result.valid ? "VALID" : `INVALID (${result.errors.length} error(s))`);
	}
	return result.valid ? 0 : 1;
}

if (import.meta.main) process.exitCode = main(process.argv.slice(2));
