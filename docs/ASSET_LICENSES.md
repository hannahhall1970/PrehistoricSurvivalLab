# Asset Licenses

| Asset/source | Current status | Rule |
| --- | --- | --- |
| Repository-authored C++ and configuration | Present | Project-owned; retain repository license and attribution policy. |
| Trex dinosaur (mesh, skeleton, animations) | Present | Quaternius "Animated LowPoly Dinosaurs" (Dec 2018), CC0 1.0; redistribution allowed incl. public repos, no attribution required. Source: <https://quaternius.itch.io/animated-lowpoly-dinosaurs>. Files: `SourceArt/Dino/Trex.fbx`, `Content/Characters/Dino/*`. |
| Stylized Nature MegaKit Standard (trees, bushes, grass, flowers, rocks, textures) | Present | Quaternius, CC0 1.0; redistribution allowed, no attribution required. Source: <https://quaternius.itch.io/stylized-nature-megakit>. Archive `megakit_standard.zip` sha256 `298f6732b872e4cf7b30e6e7abf9641c7f6dc6b326df37ac089533ed7e3d58c9`. Files: `SourceArt/Nature` (LICENSE.txt, PROVENANCE.txt), `Content/Environment`. |
| Parasaurolophus prey dinosaur (mesh, skeleton, animations) | Present | Same Quaternius "Animated LowPoly Dinosaurs" pack as the Trex (archive sha256 `d9ca7d3568bd98b323b0be9a82b546af66d77c2a18e355c1bee5c50061b8931c`), CC0 1.0. Files: `SourceArt/Prey`, `Content/Characters/Prey`. |
| Other third-party binary/content assets | None currently in repo | Do not add without recording provenance and license. |
| Unreal Engine Third Person template/starter content | Planned Editor dependency | Usable in Unreal projects under the Unreal Engine EULA; obtain through the UE Editor/launcher. |
| External sample repositories (for patterns only) | Not copied | Verify each repository's license before reuse; do not copy code or assets by assumption. |

Use Git LFS for `.uasset`, `.umap`, `.fbx`, `.png`, `.wav`, and `.exr`. Keep a source/license note when adding external content. Never fabricate or commit generated binary assets as if they were authored or tested.
