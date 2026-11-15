# Asset Licenses

| Asset/source | Current status | Rule |
| --- | --- | --- |
| Repository-authored C++ and configuration | Present | Project-owned; retain repository license and attribution policy. |
| Trex dinosaur (mesh, skeleton, animations) | Present | Quaternius "Animated LowPoly Dinosaurs" (Dec 2018), CC0 1.0; redistribution allowed incl. public repos, no attribution required. Source: <https://quaternius.itch.io/animated-lowpoly-dinosaurs>. Files: `SourceArt/Dino/Trex.fbx`, `Content/Characters/Dino/*`. |
| Stylized Nature MegaKit Standard (trees, bushes, grass, flowers, rocks, textures) | Present | Quaternius, CC0 1.0; redistribution allowed, no attribution required. Source: <https://quaternius.itch.io/stylized-nature-megakit>. Archive `megakit_standard.zip` sha256 `298f6732b872e4cf7b30e6e7abf9641c7f6dc6b326df37ac089533ed7e3d58c9`. Files: `SourceArt/Nature` (LICENSE.txt, PROVENANCE.txt), `Content/Environment`. |
| Parasaurolophus prey dinosaur (mesh, skeleton, animations) | Present | Same Quaternius "Animated LowPoly Dinosaurs" pack as the Trex (archive sha256 `d9ca7d3568bd98b323b0be9a82b546af66d77c2a18e355c1bee5c50061b8931c`), CC0 1.0. Files: `SourceArt/Prey`, `Content/Characters/Prey`. |
| Be Vietnam Pro font (Regular, SemiBold, Bold) | Present | Lâm Bảo, Tony Le, ViệtAnh Nguyễn; SIL Open Font License 1.1 (`SourceArt/Fonts/OFL.txt`, copyright "Copyright 2021 The Be Vietnam Pro Project Authors (https://github.com/bettergui/BeVietnamPro)"; no Reserved Font Name declared). Source: <https://github.com/google/fonts/tree/c13390277dfb1fcc7415d84ef7ef9cfc1e52f8c1/ofl/bevietnampro> (pinned commit c13390277dfb1fcc7415d84ef7ef9cfc1e52f8c1); TTFs unmodified, sha256 in `SourceArt/Fonts/PROVENANCE.txt`. OFL allows embedding/bundling with the game; the fonts may not be sold by themselves and OFL.txt must ship with them. Files: `SourceArt/Fonts`, `Content/UI/Fonts`. |
| Audio (music, ambience, SFX; 9 WAV) | Present | Project-original work owned by the project owner, synthesized procedurally with ffmpeg (no samples or third-party recordings); not CC0 (no public-domain dedication made). Canonical bytes pinned by sha256 in `SourceArt/Audio/PROVENANCE.txt` and `scripts/build_game_shell.py`; the 3 noise files (AMB_Wind, SFX_Bite, SFX_Eat) were generated without a seed, so reproducible regeneration needs `anoisesrc=...:seed=<n>` and new hashes. Files: `SourceArt/Audio`, `Content/Audio`. |
| Other third-party binary/content assets | None besides the rows above | Do not add without recording provenance and license. |
| Unreal Engine Third Person template/starter content | Planned Editor dependency | Usable in Unreal projects under the Unreal Engine EULA; obtain through the UE Editor/launcher. |
| External sample repositories (for patterns only) | Not copied | Verify each repository's license before reuse; do not copy code or assets by assumption. |

Use Git LFS for `.uasset`, `.umap`, `.fbx`, `.png`, `.wav`, and `.exr`. Keep a source/license note when adding external content. Never fabricate or commit generated binary assets as if they were authored or tested.

No new third-party assets were added by the polish pass. `T_PSL_PathMask` is generated (by the build/polish scripts), not imported.
