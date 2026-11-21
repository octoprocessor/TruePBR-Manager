#include "core/NormalConvention.h"

#include <cmath>
#include <gtest/gtest.h>

using namespace tpbr;

namespace
{

constexpr int kSize = 64;

/// A smooth height field, so the gradients are well defined and the correlation
/// is not dominated by a single step.
double heightAt(int x, int y)
{
    return 0.5 + 0.25 * std::sin(x * 0.30) * std::cos(y * 0.21) + 0.10 * std::sin((x + y) * 0.12);
}

double heightDX(int x, int y)
{
    return (heightAt(x + 1, y) - heightAt(x - 1, y)) * 0.5;
}

double heightDY(int x, int y)
{
    return (heightAt(x, y + 1) - heightAt(x, y - 1)) * 0.5;
}

ConventionImage makeDisplacement()
{
    ConventionImage image;
    image.width = kSize;
    image.height = kSize;
    image.rgba.resize(static_cast<size_t>(kSize) * kSize * 4);
    for (int y = 0; y < kSize; ++y)
    {
        for (int x = 0; x < kSize; ++x)
        {
            const size_t at = (static_cast<size_t>(y) * kSize + x) * 4;
            const auto v = static_cast<uint8_t>(std::lround(heightAt(x, y) * 255.0));
            image.rgba[at + 0] = v;
            image.rgba[at + 1] = v;
            image.rgba[at + 2] = v;
            image.rgba[at + 3] = 255;
        }
    }
    return image;
}

/// Build a normal map from the same height field.
/// `flipGreen` inverts G, `flipRed` inverts R, `flipHeight` inverts both — the three
/// cases the arbiter has to tell apart.
ConventionImage makeNormal(bool flipGreen, bool flipRed)
{
    ConventionImage image;
    image.width = kSize;
    image.height = kSize;
    image.rgba.resize(static_cast<size_t>(kSize) * kSize * 4);

    // Scale so the vectors are clearly non-zero but stay inside -1..1.
    constexpr double kScale = 4.0;

    for (int y = 0; y < kSize; ++y)
    {
        for (int x = 0; x < kSize; ++x)
        {
            // Skyrim / DirectX tangent space: red runs against dH/dx, green with dH/dy.
            double nx = -heightDX(x, y) * kScale;
            double ny = heightDY(x, y) * kScale;
            if (flipRed)
                nx = -nx;
            if (flipGreen)
                ny = -ny;
            nx = std::clamp(nx, -1.0, 1.0);
            ny = std::clamp(ny, -1.0, 1.0);

            const size_t at = (static_cast<size_t>(y) * kSize + x) * 4;
            image.rgba[at + 0] = static_cast<uint8_t>(std::lround((nx + 1.0) * 127.5));
            image.rgba[at + 1] = static_cast<uint8_t>(std::lround((ny + 1.0) * 127.5));
            image.rgba[at + 2] = 255;
            image.rgba[at + 3] = 255;
        }
    }
    return image;
}

ConventionImage makeFlatDisplacement()
{
    ConventionImage image;
    image.width = kSize;
    image.height = kSize;
    image.rgba.assign(static_cast<size_t>(kSize) * kSize * 4, 255);
    return image;
}

} // namespace

// The ordinary case: a correct map is left alone.
TEST(NormalConvention, CorrectGreenIsNotFlipped)
{
    const auto result = NormalConvention::analyse(makeNormal(false, false), makeDisplacement());
    EXPECT_EQ(result.verdict, NormalConventionVerdict::GreenCorrect);
    EXPECT_FALSE(result.shouldFlipGreen());
    EXPECT_GT(result.greenCorrelation, NormalConvention::kSignalThreshold);
    EXPECT_LT(result.redCorrelation, 0.0);
    EXPECT_GE(result.samples, NormalConvention::kMinSamples);
}

// The case the whole feature exists for: green inverted, red still right.
TEST(NormalConvention, InvertedGreenIsFlipped)
{
    const auto result = NormalConvention::analyse(makeNormal(true, false), makeDisplacement());
    EXPECT_EQ(result.verdict, NormalConventionVerdict::GreenFlipped);
    EXPECT_TRUE(result.shouldFlipGreen());
    EXPECT_LT(result.greenCorrelation, -NormalConvention::kSignalThreshold);
    EXPECT_LT(result.redCorrelation, 0.0);
}

// An inverted height flips BOTH channels. Flipping green there would make it worse,
// so the arbiter must refuse to act.
TEST(NormalConvention, InvertedHeightIsAmbiguous)
{
    const auto result = NormalConvention::analyse(makeNormal(true, true), makeDisplacement());
    EXPECT_EQ(result.verdict, NormalConventionVerdict::Ambiguous);
    EXPECT_FALSE(result.shouldFlipGreen());
}

// Red-only inversion is the mirror image of the interesting case: still ambiguous,
// because this tool only ever flips green.
TEST(NormalConvention, InvertedRedOnlyIsAmbiguous)
{
    const auto result = NormalConvention::analyse(makeNormal(false, true), makeDisplacement());
    EXPECT_EQ(result.verdict, NormalConventionVerdict::Ambiguous);
    EXPECT_FALSE(result.shouldFlipGreen());
}

// A flat height map carries no gradient, so there is nothing to arbitrate against.
TEST(NormalConvention, FlatDisplacementIsNotArbitrated)
{
    const auto result = NormalConvention::analyse(makeNormal(false, false), makeFlatDisplacement());
    EXPECT_EQ(result.verdict, NormalConventionVerdict::NotArbitrated);
    EXPECT_FALSE(result.shouldFlipGreen());
}

// A missing input must not be read as "green is fine".
TEST(NormalConvention, InvalidInputIsNotArbitrated)
{
    ConventionImage empty;
    const auto result = NormalConvention::analyse(empty, makeDisplacement());
    EXPECT_EQ(result.verdict, NormalConventionVerdict::NotArbitrated);
    EXPECT_FALSE(result.shouldFlipGreen());
}

// The verdict text is what reaches the status line, so it must never be null.
TEST(NormalConvention, VerdictTextIsAlwaysPresent)
{
    NormalConventionResult result;
    EXPECT_NE(result.verdictText(), nullptr);
    EXPECT_STRNE(result.verdictText(), "");
}
