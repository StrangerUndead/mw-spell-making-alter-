# CommonLibSSE-NG (alandtse/CommonLibVR, "ng" line) for the SKSE plugin, built from source as a
# subproject, configured for "flatrim": Skyrim SE (1.5.97) + AE (1.6.x) in one DLL, no VR.
#
# Source: git tag pinned below (reproducible, used by CI). To build against a local checkout
# instead (no download), pass -DLOSTART_COMMONLIB_DIR=<path> or set $COMMONLIBSSE_DIR; the
# checkout should be at the same tag.
#
# Third-party libraries CommonLib needs (spdlog, directxtk, directxmath, rapidcsv, xbyak,
# simpleini, nlohmann-json) come from vcpkg via our vcpkg.json manifest.
#
# The published prebuilt CommonLib bundles bake SE+AE+VR, so they are never used here
# (cmake/Prebuilt.cmake in CommonLib refuses a VR-less consumer and falls back to source).
include_guard(GLOBAL)
include(FetchContent)

set(LOSTART_COMMONLIB_REPOSITORY "https://github.com/alandtse/CommonLibVR.git" CACHE STRING
    "Git repository of CommonLibSSE-NG")
set(LOSTART_COMMONLIB_TAG "v10.1.0" CACHE STRING "CommonLibSSE-NG git tag/commit to build")
set(LOSTART_COMMONLIB_DIR "$ENV{COMMONLIBSSE_DIR}" CACHE PATH
    "Local CommonLibSSE-NG source tree to use instead of downloading (optional)")

# A function scope keeps the CommonLib option variables below (BUILD_TESTS, ...) from leaking
# into the rest of our project.
function(_lostart_add_commonlib)
    if(LOSTART_COMMONLIB_DIR)
        if(NOT EXISTS "${LOSTART_COMMONLIB_DIR}/cmake/CommonLibSSE.cmake")
            message(FATAL_ERROR "LOSTART_COMMONLIB_DIR='${LOSTART_COMMONLIB_DIR}' is not a CommonLibSSE-NG checkout")
        endif()
        set(FETCHCONTENT_SOURCE_DIR_COMMONLIBSSE "${LOSTART_COMMONLIB_DIR}")
        message(STATUS "CommonLibSSE-NG: using local source tree ${LOSTART_COMMONLIB_DIR}")
    else()
        message(STATUS "CommonLibSSE-NG: ${LOSTART_COMMONLIB_REPOSITORY} @ ${LOSTART_COMMONLIB_TAG}")
    endif()

    FetchContent_Declare(
        commonlibsse
        GIT_REPOSITORY "${LOSTART_COMMONLIB_REPOSITORY}"
        GIT_TAG "${LOSTART_COMMONLIB_TAG}"
        GIT_SHALLOW TRUE
        GIT_SUBMODULES ""        # extern/openvr is only needed for VR
        GIT_PROGRESS TRUE
    )

    # CommonLib options (CMP0077 NEW in its CMakeLists, so plain variables win over option()).
    set(ENABLE_SKYRIM_SE ON)
    set(ENABLE_SKYRIM_AE ON)
    set(ENABLE_SKYRIM_VR OFF)
    set(BUILD_TESTS OFF)
    set(SKSE_SUPPORT_XBYAK ON)          # Trampoline::write_branch/call with xbyak-generated thunks
    # REX::INI/JSON/TOML setting stores stay OFF: CommonLib 10.1.0's src/REX/REX.cpp defines them
    # with an MSVC-only construct (`void SettingLoad<T>(...)`) that clang-cl rejects. The plugin
    # uses SimpleIni / nlohmann-json directly instead (both are in vcpkg.json).
    set(REX_OPTION_INI OFF)
    set(REX_OPTION_JSON OFF)
    set(REX_OPTION_TOML OFF)
    set(COMMONLIB_ENABLE_IPO OFF)       # no /GL objects: faster builds, no LTCG forced on the plugin
    set(COMMONLIB_PREBUILT OFF)

    FetchContent_MakeAvailable(commonlibsse)
endfunction()
_lostart_add_commonlib()

if(NOT TARGET CommonLibSSE::CommonLibSSE)
    message(FATAL_ERROR "CommonLibSSE-NG did not define CommonLibSSE::CommonLibSSE")
endif()

# IDE grouping only.
set_target_properties(CommonLibSSE PROPERTIES FOLDER "third-party")
