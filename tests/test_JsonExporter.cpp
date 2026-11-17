#include <gtest/gtest.h>

#include <fstream>
#include <string>

#include "core/JsonExporter.h"
#include "core/ModImporter.h"
#include "core/PBRTextureSet.h"

using namespace tpbr;

// The patcher reads every coat key only inside a `multilayer: true` branch, so the flag itself has to be in the JSON or
// the coat values are inert. The exporter used to use the flag as a gate and never write it.

TEST(JsonExporter, Multilayer_TrueIsExported)
{
    PBRTextureSet ts;
    ts.name = "ice";
    ts.matchTexture = "landscape\\ice";
    ts.features.multilayer = true;
    ts.features.coatNormal = true;
    ts.params.coatStrength = 1.0f;

    const std::string out = JsonExporter::serializeEntry(ts);
    EXPECT_NE(out.find("\"multilayer\": true"), std::string::npos) << out;
    EXPECT_NE(out.find("\"coat_strength\""), std::string::npos) << out;
}

TEST(JsonExporter, Multilayer_FalseIsOmitted)
{
    PBRTextureSet ts;
    ts.name = "stone";
    ts.matchTexture = "landscape\\stone";

    const std::string out = JsonExporter::serializeEntry(ts);
    EXPECT_EQ(out.find("\"multilayer\""), std::string::npos) << out;
}

TEST(ModImporter, Multilayer_ReadFromJsonWithoutCoatFlags)
{
    // Before the fix the importer could only infer the flag from the coat flags, so a set that had multilayer on and no
    // coat flag came back as plain TruePBR.
    const std::string path = "test_multilayer_import.json";
    {
        std::ofstream f(path);
        f << R"([{"texture": "landscape\\ice", "multilayer": true, "coat_strength": 0.5}])";
    }
    std::vector<ImportDiagnostic> diagnostics;
    auto sets = ModImporter::parseJsonFile(path, ".", diagnostics);
    std::remove(path.c_str());
    ASSERT_EQ(sets.size(), 1u);
    EXPECT_TRUE(sets[0].features.multilayer);
    EXPECT_FALSE(sets[0].features.coatNormal);
}
