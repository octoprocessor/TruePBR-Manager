#include "NormalConvention.h"

#include "utils/Log.h"

#include <algorithm>
#include <cmath>

namespace tpbr
{

const char* NormalConventionResult::verdictText() const
{
    switch (verdict)
    {
    case NormalConventionVerdict::GreenCorrect:
        return "green is correct";
    case NormalConventionVerdict::GreenFlipped:
        return "green is inverted";
    case NormalConventionVerdict::Ambiguous:
        return "ambiguous (both channels read alike)";
    case NormalConventionVerdict::NotArbitrated:
    default:
        return "not arbitrated";
    }
}

namespace
{

/// Nearest-neighbour sample of the red channel into a grid, as a 0..1 height.
void sampleHeight(const ConventionImage& image, int gridW, int gridH, std::vector<double>& out)
{
    out.assign(static_cast<size_t>(gridW) * gridH, 0.0);
    for (int gy = 0; gy < gridH; ++gy)
    {
        const int sy = std::min(image.height - 1, (gy * image.height) / gridH);
        for (int gx = 0; gx < gridW; ++gx)
        {
            const int sx = std::min(image.width - 1, (gx * image.width) / gridW);
            const size_t idx = (static_cast<size_t>(sy) * image.width + sx) * 4;
            // BC4/grayscale sources repeat the value across RGB, so the luminance
            // weighting is a no-op there and gives a sane answer for a colour map.
            const double r = image.rgba[idx + 0];
            const double g = image.rgba[idx + 1];
            const double b = image.rgba[idx + 2];
            out[static_cast<size_t>(gy) * gridW + gx] = (0.299 * r + 0.587 * g + 0.114 * b) / 255.0;
        }
    }
}

/// Nearest-neighbour sample of the normal's R and G into grids, as -1..1 vectors.
void sampleNormal(const ConventionImage& image, int gridW, int gridH, std::vector<double>& nx, std::vector<double>& ny)
{
    nx.assign(static_cast<size_t>(gridW) * gridH, 0.0);
    ny.assign(static_cast<size_t>(gridW) * gridH, 0.0);
    for (int gy = 0; gy < gridH; ++gy)
    {
        const int sy = std::min(image.height - 1, (gy * image.height) / gridH);
        for (int gx = 0; gx < gridW; ++gx)
        {
            const int sx = std::min(image.width - 1, (gx * image.width) / gridW);
            const size_t idx = (static_cast<size_t>(sy) * image.width + sx) * 4;
            const size_t at = static_cast<size_t>(gy) * gridW + gx;
            nx[at] = (static_cast<double>(image.rgba[idx + 0]) / 255.0) * 2.0 - 1.0;
            ny[at] = (static_cast<double>(image.rgba[idx + 1]) / 255.0) * 2.0 - 1.0;
        }
    }
}

double pearson(const std::vector<double>& a, const std::vector<double>& b, const std::vector<size_t>& where)
{
    if (where.size() < 2)
    {
        return 0.0;
    }
    double meanA = 0.0;
    double meanB = 0.0;
    for (size_t i : where)
    {
        meanA += a[i];
        meanB += b[i];
    }
    meanA /= static_cast<double>(where.size());
    meanB /= static_cast<double>(where.size());

    double cov = 0.0;
    double varA = 0.0;
    double varB = 0.0;
    for (size_t i : where)
    {
        const double da = a[i] - meanA;
        const double db = b[i] - meanB;
        cov += da * db;
        varA += da * da;
        varB += db * db;
    }
    if (varA <= 0.0 || varB <= 0.0)
    {
        return 0.0;
    }
    return cov / std::sqrt(varA * varB);
}

} // namespace

NormalConventionResult NormalConvention::analyse(const ConventionImage& normal, const ConventionImage& displacement)
{
    NormalConventionResult result;
    if (!normal.valid() || !displacement.valid())
    {
        spdlog::debug("NormalConvention: input image invalid, no verdict");
        return result;
    }

    const int gridW = std::min({kGridSize, normal.width, displacement.width});
    const int gridH = std::min({kGridSize, normal.height, displacement.height});
    if (gridW < 4 || gridH < 4)
    {
        spdlog::debug("NormalConvention: images too small ({}x{}), no verdict", gridW, gridH);
        return result;
    }

    std::vector<double> height;
    std::vector<double> nx;
    std::vector<double> ny;
    sampleHeight(displacement, gridW, gridH, height);
    sampleNormal(normal, gridW, gridH, nx, ny);

    std::vector<double> dHdx(static_cast<size_t>(gridW) * gridH, 0.0);
    std::vector<double> dHdy(static_cast<size_t>(gridW) * gridH, 0.0);
    std::vector<size_t> interior;
    interior.reserve(static_cast<size_t>(gridW - 2) * (gridH - 2));

    for (int y = 1; y < gridH - 1; ++y)
    {
        for (int x = 1; x < gridW - 1; ++x)
        {
            const size_t at = static_cast<size_t>(y) * gridW + x;
            dHdx[at] = (height[at + 1] - height[at - 1]) * 0.5;
            dHdy[at] = (height[at + gridW] - height[at - gridW]) * 0.5;
            interior.push_back(at);
        }
    }

    result.samples = static_cast<int>(interior.size());
    if (result.samples < kMinSamples)
    {
        spdlog::debug("NormalConvention: only {} samples, below the {} needed", result.samples, kMinSamples);
        return result;
    }

    result.greenCorrelation = pearson(ny, dHdy, interior);
    result.redCorrelation = pearson(nx, dHdx, interior);

    if (std::abs(result.greenCorrelation) < kSignalThreshold)
    {
        // Noise. Refusing here is the whole point: a flip chosen on +/-0.004 would be a coin toss.
        spdlog::debug("NormalConvention: |green corr| {:.4f} below threshold {:.2f}, no verdict",
                      std::abs(result.greenCorrelation), kSignalThreshold);
        return result;
    }

    if (result.greenCorrelation <= -kSignalThreshold && result.redCorrelation <= kSignalThreshold)
    {
        result.verdict = NormalConventionVerdict::GreenFlipped;
    }
    else if (result.greenCorrelation >= kSignalThreshold && result.redCorrelation <= kSignalThreshold)
    {
        result.verdict = NormalConventionVerdict::GreenCorrect;
    }
    else
    {
        result.verdict = NormalConventionVerdict::Ambiguous;
    }

    spdlog::info("NormalConvention: green corr {:+.4f}, red corr {:+.4f}, {} samples -> {}", result.greenCorrelation,
                 result.redCorrelation, result.samples, result.verdictText());
    return result;
}

} // namespace tpbr
