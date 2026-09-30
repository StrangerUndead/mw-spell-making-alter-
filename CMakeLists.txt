# Lost Art of Spellmaking -- top-level build.
#
#   skse/core    portable C++20 library `lostart_core` (any host)
#   skse/tests   Catch2 v3 unit tests for lostart_core (native hosts)
#   skse/plugin  SKSE plugin LostArt.dll (CommonLibSSE-NG; Windows target only: MSVC or clang-cl)
#
# Presets (CMakePresets.json): linux-tests, linux-clangcl, windows-msvc. See docs/dev/BUILDING.md.
cmake_minimum_required(VERSION 3.25)

project(
    LostArt
    VERSION 1.0.0
    DESCRIPTION "Lost Art of Spellmaking"
    LANGUAGES CXX
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not supported; use a preset (cmake --preset <name>).")
endif()

get_property(_lostart_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT _lostart_multi_config AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

# --- what to build --------------------------------------------------------------------------
# The plugin needs a Windows target with an MSVC-compatible compiler (cl.exe or clang-cl).
if(CMAKE_SYSTEM_NAME STREQUAL "Windows" AND MSVC)
    set(_lostart_can_build_plugin ON)
else()
    set(_lostart_can_build_plugin OFF)
endif()
# Tests are built wherever they can be run: not when cross-compiling.
if(CMAKE_CROSSCOMPILING)
    set(_lostart_tests_default OFF)
else()
    set(_lostart_tests_default ON)
endif()

option(LOSTART_BUILD_PLUGIN "Build the SKSE plugin LostArt.dll" ${_lostart_can_build_plugin})
option(LOSTART_BUILD_TESTS "Build the lostart_core unit tests" ${_lostart_tests_default})

if(LOSTART_BUILD_PLUGIN AND NOT _lostart_can_build_plugin)
    message(FATAL_ERROR
        "LOSTART_BUILD_PLUGIN=ON needs a Windows x64 target with cl.exe or clang-cl "
        "(use the windows-msvc or linux-clangcl preset).")
endif()

# --- global compiler settings ---------------------------------------------------------------
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
    # Dynamic CRT (/MD) everywhere: must match the vcpkg triplets (x64-windows-static-md and
    # x64-windows-static-md-clangcl) and CommonLib.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
    add_compile_options(
        /utf-8
        /bigobj
        /permissive-
        /EHsc
        "$<$<CXX_COMPILER_ID:MSVC>:/Zc:preprocessor;/Zc:__cplusplus;/MP>"
        # clang-cl: silence warning classes that CommonLib's reverse-engineered headers trigger
        # by design (same list CommonLib's own clang-cl presets use).
        "$<$<CXX_COMPILER_ID:Clang>:-Wno-overloaded-virtual;-Wno-delete-non-abstract-non-virtual-dtor;-Wno-inconsistent-missing-override;-Wno-reinterpret-base-class;-Wno-nonportable-include-path;-Wno-deprecated-copy-with-user-provided-copy;-Wno-invalid-offsetof;-Wno-switch;-Wno-unused-command-line-argument;-Wno-deprecated-volatile;-Wno-unused-private-field>"
    )
endif()

# --- dependencies ---------------------------------------------------------------------------
include(LostArtDependencies)   # nlohmann_json (+ Catch2 when tests are on)

# --- components (each is owned separately; only added once it exists) ----------------------
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/skse/core/CMakeLists.txt")
    add_subdirectory(skse/core)
else()
    message(STATUS "skse/core/CMakeLists.txt not present yet -- skipping lostart_core")
endif()

if(LOSTART_BUILD_TESTS)
    enable_testing()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/skse/tests/CMakeLists.txt")
        add_subdirectory(skse/tests)
    else()
        message(STATUS "skse/tests/CMakeLists.txt not present yet -- skipping tests")
    endif()
endif()

if(LOSTART_BUILD_PLUGIN)
    include(LostArtCommonLib)  # CommonLibSSE::CommonLibSSE (SE + AE, no VR)
    add_subdirectory(skse/plugin)
endif()

message(STATUS "LostArt: plugin=${LOSTART_BUILD_PLUGIN} tests=${LOSTART_BUILD_TESTS} "
               "compiler=${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION} target=${CMAKE_SYSTEM_NAME}")
