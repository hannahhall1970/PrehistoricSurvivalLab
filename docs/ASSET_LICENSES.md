# Asset Licenses

| Asset/source | Current status | Rule |
| --- | --- | --- |
| Repository-authored C++ and configuration | Present | Project-owned; retain repository license and attribution policy. |
| Trex dinosaur (mesh, skeleton, animations) | Present | Quaternius "Animated LowPoly Dinosaurs" (Dec 2018), CC0 1.0; redistribution allowed incl. public repos, no attribution required. Source: <https://quaternius.itch.io/animated-lowpoly-dinosaurs>. Files: `SourceArt/Dino/Trex.fbx`, `Content/Characters/Dino/*`. |
| Other third-party binary/content assets | None currently in repo | Do not add without recording provenance and license. |
| Unreal Engine Third Person template/starter content | Planned Editor dependency | Usable in Unreal projects under the Unreal Engine EULA; obtain through the UE Editor/launcher. |
| External sample repositories (for patterns only) | Not copied | Verify each repository's license before reuse; do not copy code or assets by assumption. |

Use Git LFS for `.uasset`, `.umap`, `.fbx`, `.png`, `.wav`, and `.exr`. Keep a source/license note when adding external content. Never fabricate or commit generated binary assets as if they were authored or tested.
