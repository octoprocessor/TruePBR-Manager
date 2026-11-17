# Changelog

Versions are SemVer with a `0P` prefix, and this line starts at the fork point: upstream
[`v0.7.0`](https://github.com/jiayev/TruePBR-Manager/releases/tag/v0.7.0) is **0P1.0.0** here. Only the numbers are SemVer —
`MAJOR` for a change that breaks an existing `.tpbr` project or exported JSON, `MINOR` for a new capability or a batch of
behavioural fixes, `PATCH` for a small fix on its own. Upstream's own version numbers are not reused, so `0P1.x` says
"this fork, after 0.7.0".

## 0P1.1.0 — 2026-11-17

Everything the fork changed after it was taken from upstream v0.7.0. No project or JSON format break: a `.tpbr` and a
PGPatcher JSON from 0P1.0.0 still load.

### Fixed

- **`multilayer` was never written to the PGPatcher JSON, so every coat value was inert.** PGPatcher reads `coat_color`,
  `coat_specular_level`, `coat_roughness`, `coat_strength`, `coat_diffuse`, `coat_parallax`, `coat_normal` and
  `inner_uv_scale` **only** inside `if (multilayer == true)` (`PatcherMeshShaderTruePBR.cpp:1155-1216`), and outside that
  branch it resets the shader type and clears `SLSF2_MULTI_LAYER_PARALLAX`. The exporter used the flag as a gate for those
  keys and never wrote the key itself, so a set with the coat layer switched on exported coat values that did nothing. The
  key is now emitted when the flag is set, and the importer reads it instead of inferring it from the coat flags, so the
  flag survives a round trip. **A project exported by 0P1.0.0 needs one re-export** to gain the key.
- **The preview's interlayer parallax did not match Community Shaders.** The inner layer was sampled at a slightly different
  offset than CS computes (`eta`, `heightScale *= 0.5`), so a multilayer material's depth looked right in the tool and
  slightly wrong in game.
- **The fade control and the opaque toggle fought each other at 1.0.** The fade now tops out at 0.99 so the toggle owns
  "opaque" unambiguously.

### Added

- **Alpha fade toggle for the 3D preview**, so alpha coverage — what a painted vertex mask actually contributes — can be
  inspected without leaving the tool.
- **Community Shaders' real parameter ranges** in the preview, so a value that looks correct in the preview is the value the
  game reads.

### Changed

- **DDS previews decode only the mip they show.** An 8K diffuse used to decode the whole mip chain before anything appeared;
  the needed mip is decoded instead and the texture cache has a budget, which is what made an 8K-heavy project usable.
- A spelling pass over the fork's own new strings, then a revert of the part that touched the original author's text: his
  spellings in his own code stay exactly as he wrote them, and only text this fork adds follows American spelling.

## 0P1.0.0 — baseline

Upstream [`v0.7.0`](https://github.com/jiayev/TruePBR-Manager/releases/tag/v0.7.0), unchanged, as the point this fork
diverged. Tagged so the version line has an origin to count from; no release assets, because the build here is byte-for-byte
upstream's.
