# lostart_core: the portable half of the plugin (no Windows, no CommonLib). Everything that can
# be decided without the game lives here so it can be unit tested on any host.
find_package(nlohmann_json 3.11 CONFIG REQUIRED)

add_library(lostart_core STATIC
    src/Catalog.cpp
    src/Compiler.cpp
    src/Content.cpp
    src/CostEngine.cpp
    src/Discovery.cpp
    src/Mechanics.cpp
    src/MenuSession.cpp
    src/Serialization.cpp
    src/Settings.cpp
    src/Text.cpp
    src/Types.cpp
    src/Util.cpp
)
add_library(LostArt::core ALIAS lostart_core)

target_include_directories(lostart_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(lostart_core PUBLIC cxx_std_20)
target_link_libraries(lostart_core PRIVATE nlohmann_json::nlohmann_json)
set_target_properties(lostart_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    target_compile_options(lostart_core PRIVATE /W4)
else()
    target_compile_options(lostart_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wno-sign-conversion)
endif()
