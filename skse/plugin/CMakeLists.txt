# LostArt.dll -- CommonLibSSE-NG SKSE plugin (Skyrim SE 1.5.97 + AE 1.6.x, Address Library).
# Added by the top-level CMakeLists.txt only for Windows targets (MSVC or clang-cl).
#
# Every .cpp/.h under src/ is compiled; new files need no CMake change (CONFIGURE_DEPENDS
# re-globs on each build). If src/PCH.h exists it is used as the precompiled header.
file(GLOB_RECURSE LOSTART_PLUGIN_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp"
)

configure_file(
    "${PROJECT_SOURCE_DIR}/cmake/version.rc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/version.rc"
    @ONLY
)

add_library(LostArt SHARED ${LOSTART_PLUGIN_SOURCES} "${CMAKE_CURRENT_BINARY_DIR}/version.rc")

set_target_properties(LostArt PROPERTIES OUTPUT_NAME "LostArt")
target_compile_features(LostArt PRIVATE cxx_std_23)
target_include_directories(LostArt PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(LostArt PRIVATE
    LOSTART_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    LOSTART_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    LOSTART_VERSION_PATCH=${PROJECT_VERSION_PATCH}
)

target_link_libraries(LostArt PRIVATE CommonLibSSE::CommonLibSSE)
if(TARGET lostart_core)
    target_link_libraries(LostArt PRIVATE lostart_core)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/PCH.h")
    target_precompile_headers(LostArt PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/PCH.h")
endif()

if(MSVC)
    # Always write a PDB (crash-log symbolication; RelWithDebInfo has /Zi) and drop dead code.
    target_link_options(LostArt PRIVATE
        /DEBUG
        "$<$<NOT:$<CONFIG:Debug>>:/OPT:REF;/OPT:ICF>"
    )
endif()

# `cmake --install <build> --prefix <Data dir>` lays the DLL out as the game expects.
install(TARGETS LostArt RUNTIME DESTINATION "SKSE/Plugins" LIBRARY DESTINATION "SKSE/Plugins")
install(FILES "$<TARGET_PDB_FILE:LostArt>" DESTINATION "SKSE/Plugins" OPTIONAL)
