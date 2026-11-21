#include "NormalConvention.h"

#include "utils/DDSUtils.h"
#include "utils/ImageUtils.h"
#include "utils/Log.h"

#include <algorithm>

namespace tpbr
{

/// Pixel reading lives here rather than in NormalConvention.cpp because it needs
/// DirectXTex and stb, and the core library is deliberately free of both so the
/// correlation maths stays unit-testable on synthetic images.
bool NormalConvention::loadForAnalysis(const std::filesystem::path& path, ConventionImage& out, int maxSize)
{
    out = ConventionImage{};
    if (path.empty() || !std::filesystem::exists(path))
    {
        return false;
    }

    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(::tolower(c)); });

    if (ext == ".dds")
    {
        int w = 0;
        int h = 0;
        std::vector<uint8_t> pixels;
        if (!DDSUtils::loadDDSAtMaxSize(path, maxSize, w, h, pixels))
        {
            return false;
        }
        out.width = w;
        out.height = h;
        out.rgba = std::move(pixels);
        return out.valid();
    }

    auto image = ImageUtils::loadImage(path);
    if (image.pixels.empty() || image.width <= 0 || image.height <= 0)
    {
        return false;
    }
    out.width = image.width;
    out.height = image.height;
    out.rgba = std::move(image.pixels);
    return out.valid();
}

NormalConventionResult NormalConvention::analyseFiles(const std::filesystem::path& normalPath,
                                                      const std::filesystem::path& displacementPath)
{
    ConventionImage normal;
    ConventionImage displacement;
    if (!loadForAnalysis(normalPath, normal))
    {
        spdlog::warn("NormalConvention: could not read normal map {}", normalPath.string());
        return {};
    }
    if (!loadForAnalysis(displacementPath, displacement))
    {
        spdlog::warn("NormalConvention: could not read displacement map {}", displacementPath.string());
        return {};
    }
    return analyse(normal, displacement);
}

} // namespace tpbr
