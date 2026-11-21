#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace tpbr
{

/// A decoded image reduced to what the convention test needs: RGBA8, row-major.
struct ConventionImage
{
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba;

    bool valid() const
    {
        return width > 0 && height > 0 && rgba.size() == static_cast<size_t>(width) * height * 4;
    }
};

/// Verdict from comparing a normal map's R/G against a displacement map's gradient.
///
/// Why the displacement is the arbiter: the parallax renderer reads the height
/// map directly, so a normal is "correct" only when it agrees with that height.
/// The convention expected by Skyrim (and by DirectX-style maps in general) is
///     green  vs +dH/dy   -> positive
///     red    vs -dH/dx    -> negative (i.e. correlation with +dH/dx is negative)
/// An inverted *height* flips BOTH channels, so a map that reads green-negative
/// **and** red-negative has the height polarity right and exactly one wrong
/// channel -- green. That case is the only one this class will act on.
enum class NormalConventionVerdict
{
    NotArbitrated, ///< no usable evidence: unreadable file, too few samples, or noise-level signal
    GreenCorrect,  ///< green agrees with the height gradient; change nothing
    GreenFlipped,  ///< green is inverted while red is correct; flip green only
    Ambiguous,     ///< both channels read the same sign (usually inverted height); change nothing
};

struct NormalConventionResult
{
    NormalConventionVerdict verdict = NormalConventionVerdict::NotArbitrated;
    double greenCorrelation = 0.0; ///< normal G vs dH/dy
    double redCorrelation = 0.0;   ///< normal R vs dH/dx
    int samples = 0;

    bool shouldFlipGreen() const
    {
        return verdict == NormalConventionVerdict::GreenFlipped;
    }

    /// Short human-readable verdict, for status lines and logs.
    const char* verdictText() const;
};

/// Decides whether a normal map's green channel is inverted, by testing it against
/// the displacement map that ships beside it.
class NormalConvention
{
  public:
    /// Correlation magnitude below which the evidence is noise and no verdict is given.
    /// Measured separation on this project's art: unrelated pairs sit at +/-0.004,
    /// a matching pair at 0.6-0.8, so 0.10 is two orders of magnitude clear of noise.
    static constexpr double kSignalThreshold = 0.10;

    /// Interior samples needed before a verdict is allowed at all.
    static constexpr int kMinSamples = 256;

    /// Longest edge used for the analysis grid; both images are sampled to it.
    static constexpr int kGridSize = 256;

    /// Correlate the normal's channels against the displacement gradient.
    static NormalConventionResult analyse(const ConventionImage& normal, const ConventionImage& displacement);

    /// Load both files and analyse. A file that cannot be read yields NotArbitrated.
    static NormalConventionResult analyseFiles(const std::filesystem::path& normalPath,
                                               const std::filesystem::path& displacementPath);

    /// Load a texture for analysis. DDS goes through the mip selector so a 4K source
    /// costs a 512px read rather than a 67 MB one; everything else goes through stb.
    static bool loadForAnalysis(const std::filesystem::path& path, ConventionImage& out, int maxSize = 512);
};

} // namespace tpbr
