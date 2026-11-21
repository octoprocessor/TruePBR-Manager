# Changelog

Versions are SemVer with a `0P` prefix, and this line starts at the fork point: upstream
[`v0.7.0`](https://github.com/jiayev/TruePBR-Manager/releases/tag/v0.7.0) is **0P1.0.0** here. Only the numbers are SemVer —
`MAJOR` for a change that breaks an existing `.tpbr` project or exported JSON, `MINOR` for a new capability or a batch of
behavioural fixes, `PATCH` for a small fix on its own. Upstream's own version numbers are not reused, so `0P1.x` says
"this fork, after 0.7.0".

## 0P1.2.0 - 2026-11-21

Adds the authoring passes that were still manual, and makes the feature flags authoritative
over what gets written. No `.tpbr` or PGPatcher JSON format break: a project from 0P1.1.0 still
loads, and absent keys keep their old meaning.

### Added

- **The normal's green channel is decided from the displacement map.** A correlation test against
  the displacement ships beside the normal, in the same shape as the external arbiter already used
  for these textures: green should track `+dH/dy` and red `-dH/dx`, and an inverted *height* flips
  both, so a map reading green-negative **and** red-negative has exactly one wrong channel and is
  the only case acted on. It runs when both maps are present, sets the flag, and then hands the
  decision to the user -- the next click owns it and nothing overwrites it. An inconclusive test
  (below the noise floor, too few samples, unreadable file) changes nothing and retries later.
- **The same test on the coat normal's RGB**, against the same displacement.
- **Features are switched on from the maps actually present.** Fuzz, subsurface, coat colour, coat
  normal, parallax and emissive follow their slots; `multilayer` follows any coat, fuzz or glint
  feature, because PGPatcher ignores every `coat_*` key outside a multilayer entry.
- **Coat normal and roughness from two files.** A separate mode takes a coat normal and a roughness
  map, packs them into the `_cnr` the patcher expects (normal RGB with its own alpha dropped,
  roughness read from R into A, roughness resized to the normal when they differ), and toggles back
  to a single packed file without losing either path.
- **Per-texture linear inversion.** An `Inv` toggle on albedo, fuzz, subsurface, coat colour, the
  coat pair and displacement writes `255 - v` at preview and export time, for flipping a mask's
  coverage or a height map's polarity. The source file is never modified.
- **Import pickers open in the folder of whatever already occupies that slot**, falling back to a
  sibling already loaded in the set, so preloading a folder and then choosing a mask lands in the
  right place.

### Changed

- **The feature flag decides what is written.** A map whose feature is off is no longer exported,
  so it can stay loaded for reference without being written or implied by the JSON. Fuzz and the
  coat pair share a slot, and subsurface and coat colour share one, so this is what lets two maps
  sit in the same slot and still write exactly one.
- **Features that cannot coexist can no longer both be on.** Precedence follows the patcher rather
  than taste: `hair` clears everything (it owns `SLSF2_BACK_LIGHTING`, which the coat normal also
  drives and applies after it); `multilayer` clears glint, fuzz, subsurface and foliage (they are
  one `else if` chain, so one shader and the others' parameters are never read); `fuzz` clears glint
  and the coat normal (`_f.dds` is written into the same slot after `_cnr.dds`). The blocked toggle
  is switched off so the JSON stays honest, but stays visible and marked with the reason.
- **`displacementScale` now defaults to 0.5**, from 1.0.
- **`specularLevel` rises to 0.08** when the packed RMAOS alpha carries data or a non-white specular
  channel map is loaded -- and only while the value is still the default, so a hand-set value
  survives.
- **The preview and the export now run the same transforms.** The normal's G flip had been
  hand-written at three call sites and the new inversion reached none of them; both now come from
  one function, so what is drawn is what would be written.

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
