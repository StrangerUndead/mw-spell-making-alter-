# vcpkg triplet for the `linux-clangcl` preset: static libraries, dynamic CRT (/MD) -- the same
# linkage as the stock x64-windows-static-md triplet used by the `windows-msvc` preset -- but
# every port is compiled with clang-cl/lld-link from a Linux host (see the chainloaded toolchain).
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

# Release-only: our cross build is RelWithDebInfo/Release and this halves dependency build time.
set(VCPKG_BUILD_TYPE release)

# Do NOT set VCPKG_CMAKE_SYSTEM_NAME to "Windows": vcpkg only sets VCPKG_TARGET_IS_WINDOWS when it
# is undefined/empty. The chainloaded toolchain does the real targeting.
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/../toolchains/linux-clangcl.cmake")

# Let the sysroot location and the llvm-mingw location (directxtk overlay port) reach port builds.
set(VCPKG_ENV_PASSTHROUGH XWIN_SYSROOT LLVM_MINGW_BIN WINEPREFIX)
