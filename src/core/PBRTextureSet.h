#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace tpbr
{

/// PBR texture slot — corresponds to NIF TX00..TX07
enum class PBRTextureSlot
{
    Diffuse,             // Slot 1 (TX00): Base Color RGB + Opacity A
    Normal,              // Slot 2 (TX01): Normal Map RGB
    Emissive,            // Slot 3 (TX02): Glow/Emissive RGB
    Displacement,        // Slot 4 (TX03): Height R
    RMAOS,               // Slot 6 (TX05): Roughness R, Metallic G, AO B, Specular A
    CoatNormalRoughness, // Slot 7 (TX06): Coat Normal RGB + Coat Roughness A
    Fuzz,                // Slot 7 (TX06): Fuzz Color RGB + Fuzz Mask A
    Subsurface,          // Slot 8 (TX07): Subsurface Color RGB + Opacity A
    CoatColor,           // Slot 8 (TX07): Coat Color RGB + Strength A
};

/// Individual channel maps for RMAOS packing
enum class ChannelMap
{
    Roughness,
    Metallic,
    AO,
    Specular,
};

/// Defines whether RMAOS comes from a packed texture or from split channels.
enum class RMAOSSourceMode
{
    PackedTexture,
    SeparateChannels,
};

/// Defines how the coat (multilayer) normal and roughness are supplied.
///
/// `PackedTexture` is the historical `_cnr` layout: coat normal in RGB, coat
/// roughness in A.  `SeparateMaps` takes a normal map and a roughness map as two
/// files, the same shape RMAOS already offers, so a pipeline that exports
/// roughness separately does not have to pack it. Because the roughness lives in
/// its own file in that mode, the normal's **alpha channel is dropped** on export.
enum class CoatSourceMode
{
    PackedTexture,
    SeparateMaps,
};

enum class TextureMatchMode
{
    Auto,
    Diffuse,
    Normal,
};

/// Output DDS compression mode for exported textures.
enum class DDSCompressionMode
{
    BC7_sRGB,
    BC7_Linear,
    BC3_sRGB,
    BC6H_UF16,
    BC5_Linear,
    BC4_Linear,
    BC1_sRGB,
    BC1_Linear,
    RGBA8_sRGB,
    RGBA8_Linear,
};

enum class TextureAlphaMode
{
    Unknown,
    None,
    Opaque,
    Transparent,
};

/// Get the standard DDS suffix for a given slot
const char* slotSuffix(PBRTextureSlot slot);

/// Get display name for a slot
const char* slotDisplayName(PBRTextureSlot slot);

/// Get the default export compression mode for a slot.
DDSCompressionMode defaultCompressionForSlot(PBRTextureSlot slot);

/// Get UI display name for a compression mode.
const char* compressionModeDisplayName(DDSCompressionMode mode);

/// Get stable serialization key for a compression mode.
const char* compressionModeKey(DDSCompressionMode mode);

/// Parse a serialized compression mode key.
bool tryParseCompressionMode(const std::string& value, DDSCompressionMode& mode);

/// Get stable serialization key for the active RMAOS source mode.
const char* rmaosSourceModeKey(RMAOSSourceMode mode);

/// Parse a serialized RMAOS source mode key.
bool tryParseRmaosSourceMode(const std::string& value, RMAOSSourceMode& mode);

/// Get stable serialization key for the coat source mode.
const char* coatSourceModeKey(CoatSourceMode mode);

/// Parse a serialized coat source mode key.
bool tryParseCoatSourceMode(const std::string& value, CoatSourceMode& mode);

/// Get stable serialization key for the vanilla match mode.
const char* textureMatchModeKey(TextureMatchMode mode);

/// Get UI display name for the vanilla match mode.
const char* textureMatchModeDisplayName(TextureMatchMode mode);

/// Parse a serialized vanilla match mode key.
bool tryParseTextureMatchMode(const std::string& value, TextureMatchMode& mode);

/// Generate a list of export size options based on a texture's native size.
/// Returns pairs of (width, height) including the original and several
/// power-of-two scales above and below. The first entry is always the
/// original size.
std::vector<std::pair<int, int>> generateExportSizeOptions(int nativeWidth, int nativeHeight);

struct PBRTextureSet; // defined below
struct NormalConventionResult;

/// What applyAutoDefaults() changed, for a status line.
struct AutoDefaultResult
{
    bool featuresChanged = false; ///< at least one PBRFeatureFlags bit was switched on
    bool normalArbitrated = false; ///< the convention test produced a verdict
    bool normalFlipped = false;    ///< ... and it flipped the normal's green channel
    bool coatArbitrated = false;
    bool coatFlipped = false;
    float specularLevel = 0.0f; ///< value left in place, for logging
    float displacementScale = 0.0f;
    const char* conventionText = "";
    double greenCorrelation = 0.0;
    double redCorrelation = 0.0;
};

/// Fill in what the loaded maps imply. Three jobs, each guarded so a user choice survives:
///   1. switch on the feature flags that the present slots require (fuzz, subsurface,
///      coat normal, parallax, emissive, glint, model coat colour);
///   2. apply the standing defaults where the user has not touched the value --
///      a specular level still at 0.04 becomes 0.08 once real specular data exists;
///   3. decide the normal's green channel from the displacement map, and the coat
///      normal's from the same map, while each is still marked automatic.
/// Safe to call after every slot change: it is idempotent and never overwrites.
AutoDefaultResult applyAutoDefaults(PBRTextureSet& set);

// ─── Texture Entry ──────────────────────────────────────────

struct TextureEntry
{
    std::filesystem::path sourcePath; // Original imported file
    PBRTextureSlot slot{};
    int width = 0;
    int height = 0;
    int channels = 0;
    TextureAlphaMode alphaMode = TextureAlphaMode::Unknown;
    std::string format; // "png", "dds", "tga", etc.

    /// Invert every channel linearly (v -> 255 - v) at preview and export time.
    /// For a single-channel mask this flips coverage; for the displacement map it
    /// flips the height, which is what an interlayer parallax setup can need.
    /// The source file is never modified.
    bool invertLinear = false;
};

/// Two-file coat input, used when PBRTextureSet::coatSourceMode is SeparateMaps.
struct CoatSeparateMaps
{
    std::filesystem::path normalPath;    ///< Coat normal, RGB (alpha is dropped on export)
    std::filesystem::path roughnessPath; ///< Coat roughness, read from R

    int normalWidth = 0;
    int normalHeight = 0;
    int roughnessWidth = 0;
    int roughnessHeight = 0;

    bool valid() const
    {
        return !normalPath.empty() && !roughnessPath.empty();
    }
};

struct ChannelMapEntry
{
    std::filesystem::path sourcePath;
    int width = 0;
    int height = 0;
    int channels = 0;
    std::string format;
};

// ─── PBR Feature Flags ─────────────────────────────────────

struct PBRFeatureFlags
{
    bool emissive = false;
    bool parallax = false;
    bool subsurface = false;
    bool subsurfaceFoliage = false;
    bool multilayer = false;
    bool coatDiffuse = false;
    bool coatParallax = false;
    bool coatNormal = false;
    bool fuzz = false;
    bool glint = false;
    bool hair = false;
};

/// Reduce a feature set to a combination PGPatcher can actually express.
///
/// Precedence is **hair > multilayer > fuzz > glint**, and it follows from the patcher source rather
/// than from taste (`PatcherMeshShaderTruePBR.cpp`):
///   * `multilayer`, `glint` and `fuzz` are an `else if` chain, so only one of them can shape a
///     shader; the other two's parameters are never read.
///   * `hair` and `coat_normal` both drive `SLSF2_BACK_LIGHTING`, and the coat value is applied
///     after the hair one, so a coat normal silently cancels hair.
///   * `fuzz` writes `_f.dds` into the same slot after `coat_normal` writes `_cnr.dds`, so the two
///     cannot both reach a shape.
///   * `subsurface_foliage`, `subsurface` and `coat_diffuse` share the `_s.dds` slot; the user's
///     rule is that multilayer wins over subsurface and foliage, and that is what this enforces.
PBRFeatureFlags normaliseFeatureFlags(PBRFeatureFlags flags);

// ─── PBR Parameters ────────────────────────────────────────

struct PBRParameters
{
    float specularLevel = 0.04f;
    float roughnessScale = 1.0f;
    float displacementScale = 0.5f;
    float subsurfaceOpacity = 1.0f;
    std::array<float, 3> subsurfaceColor = {1.0f, 1.0f, 1.0f};

    // Emissive
    float emissiveScale = 0.0f;
    std::array<float, 3> emissiveColor = {1.0f, 1.0f, 1.0f};

    // Multilayer / Coat
    float coatStrength = 0.0f;
    float coatRoughness = 0.0f;
    float coatSpecularLevel = 0.04f;

    // Fuzz
    std::array<float, 3> fuzzColor = {1.0f, 1.0f, 1.0f};
    float fuzzWeight = 1.0f;

    // Glint
    float glintScreenSpaceScale = 0.0f;
    float glintLogMicrofacetDensity = 0.0f;
    float glintMicrofacetRoughness = 0.0f;
    float glintDensityRandomization = 0.0f;

    // Mesh tweaks
    bool vertexColors = true;
    float vertexColorLumMult = 1.0f;
    float vertexColorSatMult = 1.0f;
};

// ─── Match Alias ───────────────────────────────────────────

/// An alias vanilla texture path that shares the same PBR texture set via rename.
struct MatchAlias
{
    std::string matchTexture; // e.g. "landscape\\statics\\dirt02snow"
    TextureMatchMode matchMode = TextureMatchMode::Auto;
};

// ─── PBR Texture Set ───────────────────────────────────────

/// One PBR texture set maps to one or more vanilla texture paths.
/// The primary matchTexture is the canonical path whose stem matches the PBR texture filenames.
/// Additional vanilla paths that share this set via rename are stored in matchAliases.
struct PBRTextureSet
{
    std::string name;         // Display name (e.g. "WhiterunWoodPlank01")
    std::string matchTexture; // Vanilla diffuse path to match (e.g. "architecture\\whiterun\\wrwoodplank01")
    TextureMatchMode matchMode = TextureMatchMode::Auto;

    /// Additional vanilla texture paths that share this PBR texture set (via rename).
    std::vector<MatchAlias> matchAliases;

    /// Assigned textures per slot
    std::map<PBRTextureSlot, TextureEntry> textures;

    /// Export compression override per slot.
    std::map<PBRTextureSlot, DDSCompressionMode> exportCompression;

    /// Active RMAOS authoring mode.
    RMAOSSourceMode rmaosSourceMode = RMAOSSourceMode::PackedTexture;

    /// Individual channel maps before RMAOS packing
    std::map<ChannelMap, ChannelMapEntry> channelMaps;

    PBRFeatureFlags features;
    PBRParameters params;

    /// Optional: Landscape TXST EDIDs this texture set also applies to.
    /// If non-empty, exporter generates PBRTextureSets/<edid>.json for each.
    /// The textures are shared with NIF export — no separate landscape texture output.
    std::vector<std::string> landscapeEdids;

    /// Optional per-slot export size overrides. When set, the texture will be
    /// resized to this resolution during export. {0,0} means use original size.
    std::map<PBRTextureSlot, std::pair<int, int>> exportSize;

    /// Optional per-slot path overrides. When set, the exporter uses the given
    /// full relative path (e.g. "textures\\pbr\\custom\\tex.dds") as a PGPatcher
    /// slot command instead of deriving the path automatically.
    std::map<PBRTextureSlot, std::string> slotPathOverrides;

    /// When true, the Normal map's Green channel is inverted (255 - G) at preview
    /// and export time. The source file is never modified. Used for DirectX↔OpenGL
    /// normal map convention conversion.
    bool flipNormalG = false;

    /// True while the green-channel decision is still the tool's to make. Set false
    /// the moment the user toggles the button, after which nothing overrides it.
    /// A set that has been arbitrated once also clears it, so a reload does not
    /// silently re-decide a hand-made choice.
    bool flipNormalGAuto = true;

    /// The same decision for the coat normal's RGB, tested against displacement.
    bool flipCoatNormalG = false;
    bool flipCoatNormalGAuto = true;

    /// How the coat normal and roughness are supplied.
    CoatSourceMode coatSourceMode = CoatSourceMode::PackedTexture;

    /// The two-file coat input, used when coatSourceMode is SeparateMaps.
    CoatSeparateMaps coatMaps;

    std::string tags;
    std::string notes;
};

} // namespace tpbr
