# Linux host -> Windows x64 (MSVC ABI) cross toolchain: clang-cl + lld-link + llvm-rc/llvm-mt
# against a Windows SDK/CRT sysroot produced by
#   xwin --accept-license splat --output <dir> --use-winsysroot-style --preserve-ms-arch-notation
#
# Derived from CommonLibSSE-NG v10.1.0 cmake/toolchain-linux-clangcl.cmake. Used twice:
#   * by our own configure (VCPKG_CHAINLOAD_TOOLCHAIN_FILE in the `linux-clangcl` preset), and
#   * by every vcpkg port build (chainloaded from cmake/triplets/x64-windows-static-md-clangcl.cmake).
#
# Sysroot: -DXWIN_SYSROOT=... or $XWIN_SYSROOT; otherwise the first valid one of /opt/deps/xwin,
# /opt/xwin, $HOME/xwin-out.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

# Debian/Ubuntu may register only versioned names. Versioned names are tried newest first and
# before the unversioned one, because Ubuntu 24.04's default (unversioned) LLVM is 18 while the
# MSVC STL shipped by current xwin splats (14.44+) refuses anything older than Clang 19 (STL1000).
set(_clangcl_names clang-cl-21 clang-cl-20 clang-cl-19 clang-cl)
set(_lldlink_names lld-link-21 lld-link-20 lld-link-19 lld-link)
set(_llvmrc_names  llvm-rc-21 llvm-rc-20 llvm-rc-19 llvm-rc)
set(_llvmmt_names  llvm-mt-21 llvm-mt-20 llvm-mt-19 llvm-mt)
set(_llvmlib_names llvm-lib-21 llvm-lib-20 llvm-lib-19 llvm-lib)

find_program(CMAKE_C_COMPILER   NAMES ${_clangcl_names} REQUIRED)
find_program(CMAKE_CXX_COMPILER NAMES ${_clangcl_names} REQUIRED)
find_program(CMAKE_LINKER       NAMES ${_lldlink_names} REQUIRED)
find_program(CMAKE_AR           NAMES ${_llvmlib_names})
# Every link on this toolset runs a resource compiler and manifest tool (including CMake's
# ABI-detection try-compile); unset they default to rc/mt, which only exist in real MSVC.
find_program(CMAKE_RC_COMPILER  NAMES ${_llvmrc_names})
find_program(CMAKE_MT           NAMES ${_llvmmt_names})

# A usable sysroot has the MSVC CRT plus the Windows SDK *including the UCRT* in winsysroot
# layout ("Windows Kits/10/Lib/<ver>/ucrt/x64"); lld-link's /winsysroot finds nothing otherwise.
function(_lostart_xwin_sysroot_ok root out)
    file(GLOB _ucrt LIST_DIRECTORIES true "${root}/Windows Kits/10/Lib/*/ucrt/x64")
    if(IS_DIRECTORY "${root}/VC/Tools/MSVC" AND _ucrt)
        set(${out} TRUE PARENT_SCOPE)
    else()
        set(${out} FALSE PARENT_SCOPE)
    endif()
endfunction()

set(_xwin_sysroot "")
if(DEFINED XWIN_SYSROOT AND NOT XWIN_SYSROOT STREQUAL "")
    set(_xwin_candidates "${XWIN_SYSROOT}")          # explicit: must be valid
elseif(DEFINED ENV{XWIN_SYSROOT} AND NOT "$ENV{XWIN_SYSROOT}" STREQUAL "")
    set(_xwin_candidates "$ENV{XWIN_SYSROOT}")
else()
    set(_xwin_candidates "/opt/deps/xwin" "/opt/xwin" "$ENV{HOME}/xwin-out")
endif()
foreach(_cand IN LISTS _xwin_candidates)
    _lostart_xwin_sysroot_ok("${_cand}" _ok)
    if(_ok)
        set(_xwin_sysroot "${_cand}")
        break()
    endif()
endforeach()

if(_xwin_sysroot STREQUAL "")
    message(FATAL_ERROR
        "linux-clangcl toolchain: no usable xwin sysroot among: ${_xwin_candidates}. "
        "It needs VC/Tools/MSVC and Windows Kits/10/Lib/<ver>/ucrt/x64. Create one with "
        "`xwin --accept-license splat --output <dir> --include-debug-libs --use-winsysroot-style "
        "--preserve-ms-arch-notation` and pass -DXWIN_SYSROOT=<dir> or export XWIN_SYSROOT=<dir>.")
endif()
set(XWIN_SYSROOT "${_xwin_sysroot}")

# Forward the resolved sysroot into try_compile() projects and vcpkg port builds.
set(ENV{XWIN_SYSROOT} "${_xwin_sysroot}")
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES XWIN_SYSROOT)

# Chainloaded several times per configure: the guard keeps CACHE FORCE from re-appending.
# /EHsc lives here (not in consumer flags) because vcpkg builds each port in an isolated
# configure that inherits only this file, and clang-cl otherwise disables exceptions.
set(_xwin_compile_flags "--target=x86_64-pc-windows-msvc /winsysroot${_xwin_sysroot}")
if(NOT CMAKE_CXX_FLAGS MATCHES "winsysroot")
    set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS} ${_xwin_compile_flags}" CACHE STRING "" FORCE)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${_xwin_compile_flags} /EHsc" CACHE STRING "" FORCE)
    set(CMAKE_EXE_LINKER_FLAGS    "${CMAKE_EXE_LINKER_FLAGS} /winsysroot:${_xwin_sysroot}" CACHE STRING "" FORCE)
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} /winsysroot:${_xwin_sysroot}" CACHE STRING "" FORCE)
    set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} /winsysroot:${_xwin_sysroot}" CACHE STRING "" FORCE)
endif()
# llvm-rc preprocesses through clang, so it needs the sysroot too (winres.h, windows.h).
if(NOT CMAKE_RC_FLAGS MATCHES "winsysroot")
    set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} ${_xwin_compile_flags}" CACHE STRING "" FORCE)
endif()
