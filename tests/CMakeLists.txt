find_package(GTest CONFIG REQUIRED)

add_executable(TruePBR-Tests
    test_PBRTextureSet.cpp
    test_TextureSetValidator.cpp
    test_Project.cpp
    test_ModImporter.cpp
    test_VanillaConverter.cpp
)

set_target_properties(TruePBR-Tests PROPERTIES
    AUTOMOC OFF
    AUTORCC OFF
    AUTOUIC OFF
)

target_link_libraries(TruePBR-Tests PRIVATE
    TruePBR-Core
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(TruePBR-Tests)

# The JSON export/import tests get their own target: TruePBR-Tests does not link in this tree because the
# VanillaConverter symbols its test file needs are not part of TruePBR-Core, so adding these there would make them
# unreachable. This target only needs the core library.
add_executable(TruePBR-JsonTests
    test_JsonExporter.cpp
)

set_target_properties(TruePBR-JsonTests PROPERTIES
    AUTOMOC OFF
    AUTORCC OFF
    AUTOUIC OFF
)

target_link_libraries(TruePBR-JsonTests PRIVATE
    TruePBR-Core
    GTest::gtest_main
)

gtest_discover_tests(TruePBR-JsonTests)
