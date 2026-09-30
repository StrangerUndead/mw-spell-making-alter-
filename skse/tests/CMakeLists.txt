find_package(Catch2 3 CONFIG REQUIRED)
find_package(nlohmann_json 3.11 CONFIG REQUIRED)

add_executable(lostart_tests
    TestCatalog.cpp
    TestCompiler.cpp
    TestCost.cpp
    TestData.cpp
    TestDiscovery.cpp
    TestMechanics.cpp
    TestMenu.cpp
    TestSerialization.cpp
    TestSettings.cpp
    TestSlotPool.cpp
    TestText.cpp
)
target_link_libraries(lostart_tests PRIVATE lostart_core nlohmann_json::nlohmann_json Catch2::Catch2WithMain)
target_compile_definitions(lostart_tests PRIVATE LOSTART_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

include(Catch)
catch_discover_tests(lostart_tests)
