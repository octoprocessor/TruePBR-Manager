#include "core/PBRTextureSet.h"

#include <gtest/gtest.h>

using namespace tpbr;

// The rules under test come from PGPatcher's patcher (PatcherMeshShaderTruePBR.cpp), not from taste:
// multilayer/glint/fuzz are one else-if chain, hair and coat_normal both drive BACK_LIGHTING, and
// fuzz writes the same texture slot the coat normal needs.

TEST(FeatureFlags, HairClearsEverything)
{
    PBRFeatureFlags f;
    f.hair = true;
    f.emissive = f.parallax = f.subsurface = f.subsurfaceFoliage = f.multilayer = true;
    f.coatNormal = f.coatDiffuse = f.coatParallax = f.fuzz = f.glint = true;

    const auto n = normaliseFeatureFlags(f);
    EXPECT_TRUE(n.hair);
    EXPECT_FALSE(n.emissive);
    EXPECT_FALSE(n.parallax);
    EXPECT_FALSE(n.subsurface);
    EXPECT_FALSE(n.subsurfaceFoliage);
    EXPECT_FALSE(n.multilayer);
    EXPECT_FALSE(n.coatNormal);
    EXPECT_FALSE(n.coatDiffuse);
    EXPECT_FALSE(n.coatParallax);
    EXPECT_FALSE(n.fuzz);
    EXPECT_FALSE(n.glint);
}

TEST(FeatureFlags, MultilayerClearsGlintFuzzSubsurfaceAndFoliage)
{
    PBRFeatureFlags f;
    f.multilayer = true;
    f.glint = f.fuzz = f.subsurface = f.subsurfaceFoliage = true;

    const auto n = normaliseFeatureFlags(f);
    EXPECT_TRUE(n.multilayer);
    EXPECT_FALSE(n.glint);
    EXPECT_FALSE(n.fuzz);
    EXPECT_FALSE(n.subsurface);
    EXPECT_FALSE(n.subsurfaceFoliage);
}

// Multilayer is the only feature that clears subsurface; the coat keys survive it, because the coat
// is what multilayer exists for.
TEST(FeatureFlags, MultilayerKeepsTheCoatKeys)
{
    PBRFeatureFlags f;
    f.multilayer = true;
    f.coatNormal = f.coatDiffuse = f.coatParallax = true;

    const auto n = normaliseFeatureFlags(f);
    EXPECT_TRUE(n.coatNormal);
    EXPECT_TRUE(n.coatDiffuse);
    EXPECT_TRUE(n.coatParallax);
}

TEST(FeatureFlags, FuzzClearsGlintAndCoatNormal)
{
    PBRFeatureFlags f;
    f.fuzz = true;
    f.glint = true;
    f.coatNormal = true;

    const auto n = normaliseFeatureFlags(f);
    EXPECT_TRUE(n.fuzz);
    EXPECT_FALSE(n.glint);
    EXPECT_FALSE(n.coatNormal);
}

// Fuzz without a coat normal leaves the rest of the set alone.
TEST(FeatureFlags, FuzzKeepsUnrelatedFeatures)
{
    PBRFeatureFlags f;
    f.fuzz = true;
    f.emissive = true;
    f.parallax = true;

    const auto n = normaliseFeatureFlags(f);
    EXPECT_TRUE(n.fuzz);
    EXPECT_TRUE(n.emissive);
    EXPECT_TRUE(n.parallax);
}

TEST(FeatureFlags, GlintAloneIsKept)
{
    PBRFeatureFlags f;
    f.glint = true;

    const auto n = normaliseFeatureFlags(f);
    EXPECT_TRUE(n.glint);
}

// An empty set must stay empty: normalisation may not invent a feature.
TEST(FeatureFlags, EmptyStaysEmpty)
{
    const auto n = normaliseFeatureFlags(PBRFeatureFlags{});
    EXPECT_FALSE(n.emissive);
    EXPECT_FALSE(n.parallax);
    EXPECT_FALSE(n.subsurface);
    EXPECT_FALSE(n.subsurfaceFoliage);
    EXPECT_FALSE(n.multilayer);
    EXPECT_FALSE(n.coatNormal);
    EXPECT_FALSE(n.coatDiffuse);
    EXPECT_FALSE(n.coatParallax);
    EXPECT_FALSE(n.fuzz);
    EXPECT_FALSE(n.glint);
    EXPECT_FALSE(n.hair);
}

// Normalisation has to be idempotent, because the panel re-runs it on every toggle.
TEST(FeatureFlags, NormalisationIsIdempotent)
{
    PBRFeatureFlags f;
    f.multilayer = true;
    f.glint = true;
    f.fuzz = true;
    f.subsurface = true;

    const auto once = normaliseFeatureFlags(f);
    const auto twice = normaliseFeatureFlags(once);
    EXPECT_EQ(once.multilayer, twice.multilayer);
    EXPECT_EQ(once.glint, twice.glint);
    EXPECT_EQ(once.fuzz, twice.fuzz);
    EXPECT_EQ(once.subsurface, twice.subsurface);
}

// The default displacement is the value the user asked for, and it must not drift back.
TEST(PBRParameters, DisplacementDefaultIsHalf)
{
    PBRParameters p;
    EXPECT_FLOAT_EQ(p.displacementScale, 0.5f);
}

// Specular keeps its historical default until real specular data exists; applyAutoDefaults raises it.
TEST(PBRParameters, SpecularDefaultStaysUntilData)
{
    PBRParameters p;
    EXPECT_FLOAT_EQ(p.specularLevel, 0.04f);
}
