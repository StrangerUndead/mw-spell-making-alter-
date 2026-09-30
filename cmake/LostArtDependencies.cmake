# Third-party libraries for lostart_core and its tests.
#
# Each dependency is first looked up with find_package() (vcpkg on the Windows presets, or a
# system/package-manager install), and only downloaded with FetchContent when that fails
# (the `linux-tests` preset on a bare host). Because of FetchContent's find_package
# redirection (CMake >= 3.24), any later `find_package(nlohmann_json)` / `find_package(Catch2)`
# in skse/core or skse/tests succeeds either way, and the targets are always
#   nlohmann_json::nlohmann_json, Catch2::Catch2, Catch2::Catch2WithMain.
include_guard(GLOBAL)
include(FetchContent)

# nlohmann_json 3.12.0 -- the release asset is a trimmed source tree (~115 KB).
FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    FIND_PACKAGE_ARGS 3.11 CONFIG
)
set(JSON_Install OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(nlohmann_json)

if(LOSTART_BUILD_TESTS)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.11.0
        GIT_SHALLOW TRUE
        FIND_PACKAGE_ARGS 3 CONFIG
    )
    FetchContent_MakeAvailable(Catch2)
    # Make include(Catch) / catch_discover_tests() available in both cases.
    if(DEFINED catch2_SOURCE_DIR)
        list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
    elseif(DEFINED Catch2_DIR)
        list(APPEND CMAKE_MODULE_PATH "${Catch2_DIR}")
    endif()
endif()
