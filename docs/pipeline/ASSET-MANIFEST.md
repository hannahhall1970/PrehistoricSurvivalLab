# Asset manifest (Blender -> Unreal promotion gate)

A source-art change may be promoted to production only when every pipeline stage has evidence on disk.
`scripts/asset_manifest.ts` validates a JSON manifest (stdlib only, no deps).

```
bun scripts/asset_manifest.ts validate <manifest.json> [--root <repoRoot>] [--json]
bun scripts/asset_manifest.ts hash <file>
```

Exit codes: `0` valid, `1` invalid (every error listed as `<asset id>: <field>: <message>`; malformed JSON is also 1), `2` usage error.
Relative paths resolve against `--root` (default: cwd). Tests: `bun test scripts/asset_manifest.test.ts`.

## Manifest fields (per entry in `assets[]`)

| Field | Notes |
|---|---|
| `id`, `category`, `role` | `role` = `production` or `experimental`. `id` must be unique. |
| `source` | `path` (under `SourceArt/` or an external `.blend`/glb), `sha256`, `license_ref` (LICENSE/PROVENANCE file or text). |
| `export` | `path` (fbx), `sha256`, `exporter`, `exporter_version`, `unit_scale`, `up_axis`, `triangle_count`, `bone_count`, `lod_count`. All required for production; optional for experimental. |
| `import` | `package` (`/Game/...`), `script`, `skeleton`. `script` and `skeleton` required for production. |
| `stages` | One object per stage: `status` (`pending`/`passed`/`failed`) + `evidence` (file path). |

Extra per-stage keys: `ue_automation.tests` (non-empty list of `PSL.*` names); `human_art_review` needs `reviewer`, `date` (YYYY-MM-DD), `verdict` (`approve` to promote).

## Stage flow

`blender_export` -> `fbx_lint` -> `ue_import` -> `ue_automation` -> `packaged_capture` -> `human_art_review`

## Rules

- **Production gate**: all six stages `passed`, each with an existing evidence file, `ue_automation.tests` listed, `human_art_review.verdict == approve`, all export metadata present. Output prints `PROMOTABLE: <id>` or `NOT PROMOTABLE: <id>`.
- **Experimental**: may be partial (any stage pending, no export block). A `passed` stage still needs its evidence. It must not use a package path that overlaps a production asset's `import.package` (equal, or one nested under the other); the collision is reported against the experimental asset.
- **Hashes**: every source/export file that exists is re-hashed; mismatch with the manifest `sha256` is an error. Missing files are errors for production only.
- **Git LFS pointers**: the pointer is detected (`version https://git-lfs.github.com/spec/v1`). The manifest `sha256` is then compared with the pointer's `oid` (the hash of the real content). A production asset whose file is still a pointer is rejected, because the real bytes were not available to verify or export-lint.

## The example is deliberately NOT promotable

`asset-manifest.example.json` describes the real `SourceArt/Dino/Trex.fbx` (production T-Rex A; license `SourceArt/Dino/LICENSE.txt`, Quaternius CC0) and one experimental entry, T-Rex C (`/Game/Dev/ArtMig/TrexC`, partial; CC BY 4.0, staged glb in `~/PSL-private-assets`, no FBX yet).

In this checkout `Trex.fbx` is an LFS pointer: file sha256 `f3999464...bd72e` (132 bytes), LFS oid `db4ae6f6...ea8c` (real size 1,074,060 bytes). The manifest records the oid. `human_art_review`, `packaged_capture` and the other stages are `pending`, so the validator reports `NOT PROMOTABLE: trex-a`, lists the errors and exits 1. This is the correct, honest outcome until the evidence exists. T-Rex C is reported `NOT PROMOTABLE` too (it is experimental) but produces no errors of its own.

```
bun scripts/asset_manifest.ts validate docs/pipeline/asset-manifest.example.json --root .
```

## Mapping onto harness gates

| Stage | Produced by |
|---|---|
| `blender_export` | Blender export log/script output (e.g. `make_c.py`); records exporter, version, units, axis. |
| `fbx_lint` | FBX inspection report (triangles, bones, LODs, scale/axis). |
| `ue_import` | `scripts/import_dino.py` run log under the build lock (`UnrealEditor-Cmd -ExecutePythonScript`). |
| `ue_automation` | `bun harness/src/cli.ts gate test`: `PSL.*` tests verified by `scripts/verify_test_report.py`; list names in `tests`. |
| `packaged_capture` | Screenshot/video from a packaged build (`scripts/evidence.ts add-screenshot`). |
| `human_art_review` | Named reviewer verdict, mirroring `scripts/evidence.ts add-review` / `manual`; a worker never self-approves. |

Build/test stages run only through the harness gates (one writer, one Unreal build). The manifest only checks that their evidence exists; it does not run Unreal.
Production T-Rex A and Para must not change while T-Rex C / Para v2 stay `experimental` (see the art-migration plan).
