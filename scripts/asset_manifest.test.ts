import { afterEach, describe, expect, test } from "bun:test";
import { mkdirSync, mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join } from "node:path";
import { sha256, STAGES } from "./asset_manifest.ts";

const script = join(import.meta.dir, "asset_manifest.ts");
const tempDirs: string[] = [];

afterEach(() => {
	while (tempDirs.length) rmSync(tempDirs.pop()!, { recursive: true, force: true });
});

function cli(args: string[]) {
	const r = Bun.spawnSync([process.execPath, script, ...args], { stdout: "pipe", stderr: "pipe" });
	return { code: r.exitCode, out: r.stdout.toString() + r.stderr.toString() };
}

function put(root: string, rel: string, content: string): string {
	const p = join(root, rel);
	mkdirSync(dirname(p), { recursive: true });
	writeFileSync(p, content);
	return sha256(content);
}

// Build a repo with one fully evidenced production asset; returns root + manifest object.
function fixture() {
	const root = mkdtempSync(join(tmpdir(), "psl-manifest-"));
	tempDirs.push(root);
	const stages: Record<string, Record<string, unknown>> = {};
	for (const s of STAGES) {
		put(root, `ev/${s}.txt`, s);
		stages[s] = { status: "passed", evidence: `ev/${s}.txt` };
	}
	stages.ue_automation.tests = ["PSL.Species.Assets"];
	Object.assign(stages.human_art_review, { reviewer: "Quyen", date: "2026-11-18", verdict: "approve" });
	const asset = {
		id: "trex-a",
		category: "dino/trex",
		role: "production",
		source: { path: "SourceArt/t.blend", sha256: put(root, "SourceArt/t.blend", "blend"), license_ref: "LICENSE.txt" },
		export: {
			path: "SourceArt/t.fbx",
			sha256: put(root, "SourceArt/t.fbx", "fbx"),
			exporter: "blender",
			exporter_version: "4.2",
			unit_scale: 0.01,
			up_axis: "Z",
			triangle_count: 100,
			bone_count: 10,
			lod_count: 1,
		},
		import: { package: "/Game/Characters/Dino", script: "scripts/import_dino.py", skeleton: "/Game/Characters/Dino/SKEL_Trex" },
		stages,
	};
	return { root, asset, manifest: { assets: [asset] as Record<string, unknown>[] } };
}

function run(root: string, manifest: unknown) {
	const file = join(root, "manifest.json");
	writeFileSync(file, typeof manifest === "string" ? manifest : JSON.stringify(manifest));
	return cli(["validate", file, "--root", root]);
}

const experimental = (pkg: string) => ({
	id: "trex-c",
	category: "dino/trex",
	role: "experimental",
	source: { path: "SourceArt/c.glb", license_ref: "CC BY 4.0" },
	import: { package: pkg },
	stages: Object.fromEntries(STAGES.map((s) => [s, { status: "pending" }])),
});

describe("asset manifest", () => {
	test("valid production manifest with all evidence passes", () => {
		const { root, manifest } = fixture();
		const r = run(root, manifest);
		expect(r.out).toContain("PROMOTABLE: trex-a");
		expect(r.code).toBe(0);
	});

	test("missing evidence file fails", () => {
		const { root, asset, manifest } = fixture();
		asset.stages.packaged_capture.evidence = "ev/missing.png";
		const r = run(root, manifest);
		expect(r.code).toBe(1);
		expect(r.out).toContain("trex-a: stages.packaged_capture.evidence: file does not exist");
	});

	test("hash mismatch fails", () => {
		const { root, manifest } = fixture();
		writeFileSync(join(root, "SourceArt/t.fbx"), "tampered");
		const r = run(root, manifest);
		expect(r.code).toBe(1);
		expect(r.out).toContain("trex-a: export.sha256: mismatch");
	});

	test("relative path escaping root fails, even if the file exists (REV-6)", () => {
		const { root, asset, manifest } = fixture();
		put(root, "../outside-ev.txt", "x");
		tempDirs.push(join(root, "../outside-ev.txt"));
		asset.stages.packaged_capture.evidence = "../outside-ev.txt";
		const r = run(root, manifest);
		expect(r.code).toBe(1);
		expect(r.out).toContain("trex-a: stages.packaged_capture.evidence: relative path escapes root");
	});

	test("a directory as source reports a field error and keeps other errors", () => {
		const { root, asset, manifest } = fixture();
		mkdirSync(join(root, "SourceArt/dir.fbx"));
		asset.export.path = "SourceArt/dir.fbx";
		asset.stages.human_art_review.verdict = "reject";
		const r = run(root, manifest);
		expect(r.code).toBe(1);
		expect(r.out).toContain("trex-a: export.path: not a regular file");
		expect(r.out).toContain("stages.human_art_review.verdict");
	});

	test("production without human approve fails", () => {
		const { root, asset, manifest } = fixture();
		asset.stages.human_art_review.verdict = "reject";
		const r = run(root, manifest);
		expect(r.code).toBe(1);
		expect(r.out).toContain("human_art_review.verdict: production requires approve");
	});

	test("pending production stage is not promotable", () => {
		const { root, asset, manifest } = fixture();
		asset.stages.fbx_lint.status = "pending";
		const r = run(root, manifest);
		expect(r.code).toBe(1);
		expect(r.out).toContain("NOT PROMOTABLE: trex-a");
	});

	test("partial experimental asset is valid", () => {
		const { root, manifest } = fixture();
		manifest.assets.push(experimental("/Game/Dev/ArtMig/TrexC"));
		const r = run(root, manifest);
		expect(r.code).toBe(0);
	});

	test("experimental colliding with production package fails", () => {
		for (const pkg of ["/Game/Characters/Dino", "/Game/Characters/Dino/Sub"]) {
			const { root, manifest } = fixture();
			manifest.assets.push(experimental(pkg));
			const r = run(root, manifest);
			expect(r.code).toBe(1);
			expect(r.out).toContain("trex-c: import.package");
			expect(r.out).toContain("collides with production package");
		}
	});

	test("duplicate asset id fails", () => {
		const { root, manifest } = fixture();
		manifest.assets.push(experimental("/Game/Dev/A"), experimental("/Game/Dev/B"));
		const r = run(root, manifest);
		expect(r.code).toBe(1);
		expect(r.out).toContain("trex-c: id: duplicate asset id");
	});

	test("LFS pointer production source fails, oid compared", () => {
		const { root, asset, manifest } = fixture();
		const oid = "a".repeat(64);
		writeFileSync(join(root, "SourceArt/t.blend"), `version https://git-lfs.github.com/spec/v1\noid sha256:${oid}\nsize 5\n`);
		asset.source.sha256 = oid;
		const r = run(root, manifest);
		expect(r.code).toBe(1);
		expect(r.out).toContain("source.path: is a Git LFS pointer");
		expect(r.out).not.toContain("source.sha256");
	});

	test("malformed JSON reports message and exits non-zero", () => {
		const { root } = fixture();
		const r = run(root, "{ not json");
		expect(r.code).toBe(1);
		expect(r.out).toContain("manifest.json");
		expect(r.out).toContain("INVALID");
	});

	test("usage errors exit 2", () => {
		expect(cli([]).code).toBe(2);
		expect(cli(["validate"]).code).toBe(2);
		expect(cli(["validate", "x.json", "--bogus"]).code).toBe(2);
	});

	test("hash helper prints sha256", () => {
		const root = mkdtempSync(join(tmpdir(), "psl-manifest-"));
		tempDirs.push(root);
		const h = put(root, "f.txt", "hello");
		const r = cli(["hash", join(root, "f.txt")]);
		expect(r.code).toBe(0);
		expect(r.out).toContain(h);
	});
});
