#include "PBRTextureSet.h"

#include "NormalConvention.h"
#include "utils/Log.h"

namespace tpbr
{

namespace
{

/// True when a file carries any non-opaque alpha. On a packed RMAOS that means the
/// specular channel holds real data; on a BC1/BC7-without-alpha it reads 255 everywhere.
bool hasAlphaData(const std::filesystem::path& path)
{
    ConventionImage image;
    if (!NormalConvention::loadForAnalysis(path, image))
    {
        return false;
    }
    for (size_t i = 3; i < image.rgba.size(); i += 4)
    {
        if (image.rgba[i] != 255)
        {
            return true;
        }
    }
    return false;
}

/// True when a single-channel map holds any value below white.
bool hasNonWhiteValue(const std::filesystem::path& path)
{
    ConventionImage image;
    if (!NormalConvention::loadForAnalysis(path, image))
    {
        return false;
    }
    for (size_t i = 0; i < image.rgba.size(); i += 4)
    {
        if (image.rgba[i] != 255)
        {
            return true;
        }
    }
    return false;
}

/// True when the set carries specular information worth raising the level for.
bool hasSpecularData(const PBRTextureSet& set)
{
    const auto rmaos = set.textures.find(PBRTextureSlot::RMAOS);
    if (rmaos != set.textures.end() && !rmaos->second.sourcePath.empty())
    {
        return hasAlphaData(rmaos->second.sourcePath);
    }

    const auto spec = set.channelMaps.find(ChannelMap::Specular);
    if (spec != set.channelMaps.end() && !spec->second.sourcePath.empty())
    {
        return hasNonWhiteValue(spec->second.sourcePath);
    }

    return false;
}

/// Path of the coat normal currently in force, whichever mode supplies it.
std::filesystem::path coatNormalPath(const PBRTextureSet& set)
{
    if (set.coatSourceMode == CoatSourceMode::SeparateMaps && !set.coatMaps.normalPath.empty())
    {
        return set.coatMaps.normalPath;
    }
    const auto cnr = set.textures.find(PBRTextureSlot::CoatNormalRoughness);
    return cnr != set.textures.end() ? cnr->second.sourcePath : std::filesystem::path{};
}

} // namespace

AutoDefaultResult applyAutoDefaults(PBRTextureSet& set)
{
    AutoDefaultResult out;

    // ─── 1. Features implied by the slots that are present ───
    const auto present = [&set](PBRTextureSlot slot)
    {
        const auto it = set.textures.find(slot);
        return it != set.textures.end() && !it->second.sourcePath.empty();
    };

    auto enable = [&out, &set](bool& flag)
    {
        if (!flag)
        {
            flag = true;
            out.featuresChanged = true;
        }
    };

    if (present(PBRTextureSlot::Emissive))
        enable(set.features.emissive);
    if (present(PBRTextureSlot::Displacement))
        enable(set.features.parallax);
    if (present(PBRTextureSlot::Subsurface))
        enable(set.features.subsurface);
    if (present(PBRTextureSlot::Fuzz))
        enable(set.features.fuzz);
    if (present(PBRTextureSlot::CoatColor))
        enable(set.features.coatDiffuse);
    if (present(PBRTextureSlot::CoatNormalRoughness) ||
        (set.coatSourceMode == CoatSourceMode::SeparateMaps && set.coatMaps.valid()))
        enable(set.features.coatNormal);

    // PGPatcher reads every coat_* key only inside a multilayer entry, so a coat
    // feature without the flag is a set of dead values. Fuzz and glint reach the
    // multilayer path by themselves; the flag is what makes the coat keys work.
    if (set.features.coatDiffuse || set.features.coatParallax || set.features.coatNormal || set.features.fuzz ||
        set.features.glint)
        enable(set.features.multilayer);

    // ─── 2. Defaults, applied only where the user has not touched the value ───
    // 0.04 and 1.0 are the historical defaults; anything else is a choice and is kept.
    if (hasSpecularData(set) && set.params.specularLevel == 0.04f)
    {
        set.params.specularLevel = 0.08f;
    }
    if (present(PBRTextureSlot::Displacement) && set.params.displacementScale == 1.0f)
    {
        // A project written before the default moved still carries the old 1.0.
        set.params.displacementScale = 0.5f;
    }
    out.specularLevel = set.params.specularLevel;
    out.displacementScale = set.params.displacementScale;

    // ─── 3. Normal green channel, decided from the displacement map ───
    // Only while the decision is still the tool's, and only once both maps exist.
    const auto normal = set.textures.find(PBRTextureSlot::Normal);
    const auto displacement = set.textures.find(PBRTextureSlot::Displacement);
    const bool havePair = normal != set.textures.end() && displacement != set.textures.end() &&
                          !normal->second.sourcePath.empty() && !displacement->second.sourcePath.empty();

    if (set.flipNormalGAuto && havePair)
    {
        const auto result =
            NormalConvention::analyseFiles(normal->second.sourcePath, displacement->second.sourcePath);
        out.conventionText = result.verdictText();
        out.greenCorrelation = result.greenCorrelation;
        out.redCorrelation = result.redCorrelation;
        if (result.verdict != NormalConventionVerdict::NotArbitrated)
        {
            // A verdict settles it: from here the flag is the user's, and their next
            // click flips it rather than being overwritten on the next load.
            set.flipNormalGAuto = false;
            set.flipNormalG = result.shouldFlipGreen();
            out.normalArbitrated = true;
            out.normalFlipped = set.flipNormalG;
        }
    }

    if (set.flipCoatNormalGAuto)
    {
        const auto coatPath = coatNormalPath(set);
        if (!coatPath.empty() && havePair)
        {
            const auto result = NormalConvention::analyseFiles(coatPath, displacement->second.sourcePath);
            if (result.verdict != NormalConventionVerdict::NotArbitrated)
            {
                set.flipCoatNormalGAuto = false;
                set.flipCoatNormalG = result.shouldFlipGreen();
                out.coatArbitrated = true;
                out.coatFlipped = set.flipCoatNormalG;
            }
        }
    }

    return out;
}

} // namespace tpbr
