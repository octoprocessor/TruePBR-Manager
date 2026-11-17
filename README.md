**English** | [中文](README_zh.md) *(upstream's translation)*

# TruePBR Manager — optimized fork

**[octoprocessor](https://github.com/octoprocessor)'s fork of [jiayev/TruePBR-Manager](https://github.com/jiayev/TruePBR-Manager).**

Upstream owns this tool and its design. This fork carries performance and correctness changes made while authoring a
large True PBR landscape mod — a project with 8K sources, seasonal variants and coat materials, which is what exposed the
problems below. Everything from *Current Status* down is upstream's documentation, kept as it is; the part above it is the
difference.

Upstream's CI: [![CI](https://github.com/jiayev/TruePBR-Manager/actions/workflows/ci.yml/badge.svg)](https://github.com/jiayev/TruePBR-Manager/actions/workflows/ci.yml)
[![Release](https://github.com/jiayev/TruePBR-Manager/actions/workflows/release.yml/badge.svg)](https://github.com/jiayev/TruePBR-Manager/actions/workflows/release.yml)
— the workflows exist in this fork too, but Actions has never run here, so the badges are upstream's rather than showing
a permanent "no status".

## What this fork changes

| commit | what it changes, and why it mattered |
|---|---|
| `be0858b` | **Preview decodes only the mip it shows.** An 8K diffuse used to decode the whole mip chain before anything appeared; now the needed mip is decoded and the cache has a budget, so switching sets in an 8K project stops stalling. |
| `c946250` | **Alpha fade toggle for the 3D preview, and Community Shaders' real parameter ranges.** The preview's ranges now match what CS actually reads, so a value that looks right in the preview is right in game, and the fade toggle makes alpha coverage inspectable — the thing that decides whether a painted vertex mask is doing anything. |
| `48ade38` | **Interlayer parallax mirrored exactly.** The inner layer was sampled at a slightly different offset than CS computes (`eta`, `heightScale *= 0.5`), so a multilayer material's depth preview drifted from the game. |
| `cd3436d` | **Fade tops out at 0.99.** At 1.0 the fade slider and the "opaque" toggle fought for the same state and the toggle lost. |
| `b61e89d` | **The `multilayer` flag is now exported and imported.** PGPatcher reads every `coat_*` key *only* inside `if (multilayer == true)` (`PatcherMeshShaderTruePBR.cpp:1155-1216`) and outside that branch it resets the shader and **clears** `SLSF2_MULTI_LAYER_PARALLAX`. The exporter used the flag only as a gate for the coat keys and never wrote the key, so every coat value a project produced was inert in game, and on re-import the flag had to be guessed from `coat_normal`/`coat_diffuse`/`coat_parallax`. `multilayer: true` is now written when set and read back, so a project round-trips through PGPatcher JSON without losing it. |
| `c5eec56` / `eed6372` | A spelling pass over new strings and its revert: the creator's British spellings in his own code stay as he wrote them (`cancelled`, `colour` where he used them); only text this fork adds follows American spelling. |

## Getting a build

There are no automated releases here. Build from source with the same toolchain upstream documents (`## Building` below),
or take a packaged build from `dist\_release\` in a checkout — that folder holds
`TruePBR-Manager-0.7.0-optimized-win64.zip` and its `.sha256`, built locally with MSVC and `windeployqt`.

Two things worth knowing before reporting a problem here:

* **A rebuild is a new binary.** Windows Defender/Avast give each unsigned build its own sandbox evaluation; a hash-based
  exception from an earlier build does not carry over, and a path-based one only sticks if the path does not change.
* **Re-export after updating.** A `.tpbr` project saved by an older build keeps the coat flags but not the `multilayer`
  key; reopen and re-export once so the flag is written.

## Versioning

Releases here are SemVer with a `0P` prefix — `0P1.1.0` — and the line starts at the fork point: upstream `v0.7.0`, as
forked, is **0P1.0.0**. The numbers are SemVer: `MAJOR` when a `.tpbr` project or an exported JSON stops loading, `MINOR`
for new capability or a batch of behavioural fixes, `PATCH` for a single small fix. Upstream's numbers are not reused, so
`0P1.x` always means "this fork, after 0.7.0". [`CHANGELOG.md`](CHANGELOG.md) carries the detail per release.

## Licence and credit

Unchanged from upstream: [CC BY-NC 4.0](LICENSE). The tool, its architecture and its documentation are
[jiayev](https://github.com/jiayev)'s work; the changes listed above are
[octoprocessor](https://github.com/octoprocessor)'s. Issues and pull requests about the tool itself belong upstream.

---

## Upstream documentation

TruePBR Manager is a Windows desktop tool for assembling and exporting Skyrim True PBR texture sets compatible with [Community Shaders True PBR](https://github.com/doodlum/skyrim-community-shaders/wiki/True-PBR) and [PGPatcher](https://github.com/hakasapl/PGPatcher).

Each texture set maps to one vanilla diffuse path and stores the textures, feature flags, parameters, and export settings needed to generate a mod-ready True PBR package.


## Current Status

The application currently supports:

- Project save/load (`.tpbr` JSON), multi-texture-set management
- Drag-and-drop texture import (DDS, PNG, TGA, BMP, JPG) with per-slot Import/Clear
- RMAOS authoring: packed or split-channel with auto-packing on export
- Full True PBR feature editing: emissive, parallax, subsurface, coat, fuzz, glint, hair, etc.
- Per-slot DDS compression, export size override, and slot path override
- 2D preview with zoom, pan, and per-channel isolation
- 3D material preview (D3D12 Cook-Torrance PBR): IBL with HDRI, skybox, TAA, HDR output, GT7 tone mapping, ACEScg pipeline
- 3D debug channel visualization: Normal, Roughness, Metallic, AO, Specular
- Full mipmap generation for preview textures with adjustable mip LOD bias
- Batch import by filename suffix
- Import existing PBR mod: reads PGPatcher JSON from a mod directory and resolves DDS textures to reconstruct a project
- Pre-export validation (resolution, missing slots, conflicts)
- Landscape texture set support (TXST EDIDs)
- PGPatcher JSON and DDS texture export
- Export runs on a background thread with progress dialog (cancellable)
- Skips unchanged textures on re-export (timestamp + format check)
- Vanilla texture set conversion: converts Blinn-Phong textures (Diffuse, Normal, Specular, EnvMask, Cubemap, Glow, Parallax, BackLight) to True PBR with automatic RMAOS generation, cubemap metallic tint, per-texture gamma/brightness, and real-time output preview
- Conversion runs on a background thread with progress dialog (cancellable)
- Recent projects list (File menu, max 10 entries)
- 3D preview settings persisted across sessions (light, HDRI, render flags, etc.)
- Localization: auto-detects system language, supports runtime language switching, and hot-reloads translation files during development

The application does not currently provide undo/redo.

## Texture Slots

| Slot | Suffix | Content | Notes |
|------|--------|---------|-------|
| Diffuse | `.dds` | Base Color (RGB) + Opacity (A) | Required |
| Normal | `_n.dds` | Normal Map (RGB) | Required |
| RMAOS | `_rmaos.dds` | Roughness (R) Metallic (G) AO (B) Specular (A) | Required, can be imported packed or built from split channels |
| Emissive | `_g.dds` | Glow Color | Optional |
| Displacement | `_p.dds` | Height / Parallax | Optional |
| Subsurface | `_s.dds` | Subsurface Color (RGB) + Opacity (A) | Shares TX07 with coat color |
| Coat Normal Roughness | `_cnr.dds` | Coat Normal (RGB) + Coat Roughness (A) | Shares TX06 with fuzz |
| Fuzz | `_f.dds` | Fuzz Color (RGB) + Fuzz Mask (A) | Shares TX06 with coat normal |
| Coat Color | `_s.dds` | Coat Color (RGB) + Strength (A) | Shares TX07 with subsurface |

## Typical Workflow

1. Create a project and add one or more texture sets.
2. Set the vanilla match path for each set, for example `architecture\whiterun\wrwoodplank01`.
3. Import required slots and any optional textures needed by the enabled feature set.
4. For RMAOS, choose either `Packed RMAOS` or `Split Channels`.
5. Adjust material parameters and per-slot export compression.
6. Export to a mod folder to generate DDS files and `PBRNIFPatcher/<project>.json`.

## Export Layout

```text
<mod_folder>/
├── PBRNIFPatcher/
│   └── <project>.json
└── textures/
    └── pbr/
        └── <vanilla_path>/
            ├── texture.dds
            ├── texture_n.dds
            ├── texture_rmaos.dds
            └── ...
```

Example match path:

```text
architecture\whiterun\wrwoodplank01
```

Exports to:

```text
textures/pbr/architecture/whiterun/
    wrwoodplank01.dds
    wrwoodplank01_n.dds
    wrwoodplank01_rmaos.dds
```

## Building

## Pre-commit

The repository includes a pre-commit configuration focused on automatic formatting and basic text hygiene:

- `clang-format` for C and C++ source files under `src/`
- `end-of-file-fixer` to normalize the final newline
- `trailing-whitespace` to trim trailing spaces while preserving Markdown hard line breaks
- `check-yaml`, `check-merge-conflict`, `check-case-conflict`, and `check-illegal-windows-names`

If you are using `prek`, install the hook scripts with:

```powershell
prek install
```

To format and normalize the whole repository once:

```powershell
prek run --all-files
```

If you are using the reference `pre-commit` CLI instead, the equivalent commands are:

```powershell
pre-commit install
pre-commit run --all-files
```

## GitHub Actions

The repository now includes two GitHub Actions workflows:

- [`CI`](https://github.com/jiayev/TruePBR-Manager/actions/workflows/ci.yml): runs `prek` on every pull request and on pushes to `main`, then builds the application on `windows-latest`
- [`Release`](https://github.com/jiayev/TruePBR-Manager/actions/workflows/release.yml): triggers on tags matching `v*`, builds the distributable, zips `dist/TruePBR-Manager/`, and uploads it to the GitHub Release page

Recommended baseline for this project:

- `CI` for formatting, lint-style checks, and build validation
- `Release` for tagged packaging and downloadable binaries
- Unit tests can be run locally via `cmake --preset test`

### Requirements

- Windows x64
- Visual Studio 2022 or newer with the Desktop C++ workload
- CMake 3.21 or newer
- vcpkg with `VCPKG_ROOT` set

### Quick Build

```bat
set VCPKG_ROOT=C:\path\to\vcpkg
build.bat            # Release build (default)
build.bat debug      # Debug build
build.bat release    # Explicit release build
```

The script configures the project in `build/` and builds the distributable into `dist/TruePBR-Manager/`. That folder is also populated with required runtime DLLs, Qt platform plugins, precompiled shaders, and `LICENSE`.

### CMake Presets

```powershell
$env:VCPKG_ROOT = 'C:\path\to\vcpkg'
cmake --preset default
cmake --build --preset release
```

Available configure presets:

- `default` for Release
- `debug` for Debug
- `test` for Debug with unit tests

### Running Tests

```powershell
cmake --preset test
cmake --build --preset test
ctest --preset test
```

### Manual CMake

```powershell
cmake -S . -B build -G Ninja `
    -DCMAKE_BUILD_TYPE=Release `
    -DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake `
    -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build build
```

## Dependencies

Managed through vcpkg:

| Library | Purpose |
|---------|---------|
| Qt 6 Widgets | Desktop UI |
| DirectXTex | DDS metadata, decode, and encode |
| nlohmann/json | Project and export JSON serialization |
| spdlog | Logging |
| stb_image | Raster image loading |
| Google Test | Unit testing (optional, test preset only) |
Additional dependencies:

| Library | Purpose | Source |
|---------|---------|--------|
| tinyexr | EXR image loading for HDRI | Vendored (v1.0.9) |
| D3D12 / DXGI | GPU rendering (3D preview) | Windows SDK |
## Project Layout

```text
src/
├── app/        MainWindow and application shell
├── core/       Project model, texture import, RMAOS packing, JSON export, mod export, mod import, vanilla conversion, landscape export, translation manager, app settings
├── renderer/   D3D12 GPU backend, IBL pipeline, mesh generation, HLSL shaders
├── ui/         Texture set list, slot editor, feature toggles, parameter editor, vanilla conversion dialog, 3D preview widget
└── utils/      DDS helpers, image loading, file helpers, logging
tests/          Unit tests (Google Test): PBRTextureSet, TextureSetValidator, Project, VanillaConverter
translations/   JSON translation files (en.json, zh_CN.json, ...)
```

## Implementation Notes

- Projects are saved as JSON with the `.tpbr` extension.
- RMAOS source mode is persisted per texture set as either packed or split-channel mode.
- Export compression can be overridden per slot and is stored in the project file.
- Export size can be overridden per slot (power-of-two downscale).
- Slot path overrides allow custom PGPatcher `slotN` export paths per texture set.
- Alpha mode is detected during import and influences available compression options (BC1 requires no alpha).
- Translations use a custom JSON format loaded by `TranslationManager`. Place `<locale>.json` files in the `translations/` directory next to the executable. The application auto-detects the system language and supports runtime switching via the menu bar.
- Application-level settings (language, window geometry, last project directory) are persisted in `TruePBR-Manager.ini` next to the executable via `AppSettings`.
- Source DDS files already matching the target compression format and mipmap count are copied without re-encoding.
- 2D preview shows the diffuse texture by default; click any slot to preview it with channel isolation.
- 3D preview uses a rewritten D3D12 renderer with double-buffered frames, async texture upload queue, and GPU compute IBL pipeline.
- Shaders are precompiled to `.cso` during build; the renderer works in ACEScg color space throughout.
- PGPatcher JSON uses `rename` when the texture set name differs from the vanilla stem, and emits explicit `slotN` paths only when the generated path differs from PGPatcher's convention-based inference.

## Roadmap

Planned features:

- [x] Built-in vanilla texture set conversion
- [x] Import existing PBR mod (read mod directory with PGPatcher JSON + textures, reconstruct project)
- [ ] Undo/redo

## References

- [True PBR Specification](https://github.com/doodlum/skyrim-community-shaders/wiki/True-PBR)
- [PGPatcher Mod Authors Guide](https://github.com/hakasapl/PGPatcher/wiki/Mod-Authors)
- [Community Shaders](https://github.com/doodlum/skyrim-community-shaders)

## License

This project is licensed under the [Creative Commons Attribution-NonCommercial 4.0 International License (CC BY-NC 4.0)](https://creativecommons.org/licenses/by-nc/4.0/).

See [LICENSE](LICENSE) for details.
