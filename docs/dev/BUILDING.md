# Building Lost Art of Spellmaking (C++ parts)

Three CMake presets (`CMakePresets.json`) cover every build:

| Preset | Host | Builds | Toolchain | Dependencies |
| --- | --- | --- | --- | --- |
| `linux-tests` | Linux (any host with g++) | `lostart_core` + Catch2 unit tests | native g++ | `find_package`, else FetchContent (nlohmann_json 3.12.0, Catch2 3.11.0) |
| `linux-clangcl` | Linux | `LostArt.dll` (PE32+ x86-64) | clang-cl + lld-link ≥ 19, xwin sysroot | vcpkg manifest (`vcpkg.json`) + CommonLibSSE-NG via FetchContent |
| `windows-msvc` | Windows | `LostArt.dll` + unit tests | MSVC `cl.exe` (VS 2022 17.10+) | vcpkg manifest + CommonLibSSE-NG via FetchContent |

Build trees go to `build/<preset>/` (ignored by git). The DLL is
`build/<preset>/skse/plugin/LostArt.dll` (+ `LostArt.pdb`).

## What gets built, and how CommonLib is consumed

- `skse/core` (`lostart_core`) is portable C++20 and builds everywhere.
- `skse/tests` builds only when not cross-compiling (`LOSTART_BUILD_TESTS`).
- `skse/plugin` builds only for a Windows target with cl.exe or clang-cl (`LOSTART_BUILD_PLUGIN`).
- **CommonLibSSE-NG** ([alandtse/CommonLibVR](https://github.com/alandtse/CommonLibVR), ng line) is
  pulled by `cmake/LostArtCommonLib.cmake` with FetchContent at a pinned tag (`v10.1.0`) and built
  from source as a subproject, configured for **SE + AE, no VR** (`ENABLE_SKYRIM_SE=ON`,
  `ENABLE_SKYRIM_AE=ON`, `ENABLE_SKYRIM_VR=OFF`), with `SKSE_SUPPORT_XBYAK`, `REX_OPTION_INI`
  and `REX_OPTION_JSON` on.
  - There is no maintained vcpkg registry port for current CommonLibSSE-NG (the old
    colorglass registry stops at 3.x), and CommonLib's published prebuilt bundles bake in VR,
    so a pinned source build is the reproducible option.
  - To use a local checkout instead of downloading: `-DLOSTART_COMMONLIB_DIR=/path/to/commonlib`
    or `export COMMONLIBSSE_DIR=/path/to/commonlib` (it should be at the same tag).
- **Third-party libraries** CommonLib and we need (spdlog, directxtk, directxmath, rapidcsv,
  simpleini, xbyak, nlohmann-json, catch2) come from vcpkg in manifest mode. The registry
  baseline is pinned in `vcpkg-configuration.json` (a git registry, so the local vcpkg clone may
  be shallow).

## Linux: unit tests (`linux-tests`)

Needs: CMake ≥ 3.25, Ninja, g++ ≥ 12, git, network (first configure only).

```bash
cmake --preset linux-tests
cmake --build --preset linux-tests
ctest --preset linux-tests
```

## Linux: cross-compile LostArt.dll (`linux-clangcl`)

### One-time host setup

```bash
# 1. LLVM >= 19. The MSVC STL in current SDK splats (14.44+) rejects Clang 18 (error STL1000).
#    Ubuntu 24.04: clang-cl lives in clang-tools-19.
sudo apt-get install -y clang-19 clang-tools-19 lld-19 llvm-19 cmake ninja-build git curl zip unzip tar pkg-config

# 2. Windows SDK + MSVC CRT sysroot with xwin (all three flags matter; the UCRT must be present).
curl -sSL https://github.com/Jake-Shadle/xwin/releases/download/0.10.0/xwin-0.10.0-x86_64-unknown-linux-musl.tar.gz \
  | tar -xz --strip-components=1 -C /usr/local/bin xwin-0.10.0-x86_64-unknown-linux-musl/xwin
xwin --accept-license --http-retry 10 --cache-dir /opt/deps/xwin-cache splat \
  --output /opt/deps/xwin --include-debug-libs --use-winsysroot-style --preserve-ms-arch-notation
export XWIN_SYSROOT=/opt/deps/xwin

# 3. Wine + llvm-mingw: only to build DirectXTK's HLSL shaders (there is no fxc.exe in an xwin
#    sysroot). Our overlay port cmake/ports/directxtk builds the fxc2 stand-in with llvm-mingw
#    and runs DirectXTK's CompileShaders.cmd under Wine.
sudo apt-get install -y --no-install-recommends wine wine64
WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml=" wineboot -u      # create ~/.wine once
curl -sSL https://github.com/mstorsjo/llvm-mingw/releases/download/20260826/llvm-mingw-20260826-ucrt-ubuntu-22.04-x86_64.tar.xz \
  | tar -xJ -C /opt/deps && mv /opt/deps/llvm-mingw-20260826-ucrt-ubuntu-22.04-x86_64 /opt/deps/llvm-mingw
export LLVM_MINGW_BIN=/opt/deps/llvm-mingw/bin    # /opt/deps/llvm-mingw/bin and /opt/llvm-mingw/bin are also searched

# 4. vcpkg
git clone https://github.com/microsoft/vcpkg /opt/deps/vcpkg
/opt/deps/vcpkg/bootstrap-vcpkg.sh -disableMetrics
export VCPKG_ROOT=/opt/deps/vcpkg
```

Do **not** put llvm-mingw's `bin/` on `PATH`: it ships its own `clang-cl`/`lld-link`, which
would shadow the real toolchain.

### Build

```bash
export VCPKG_ROOT=/opt/deps/vcpkg XWIN_SYSROOT=/opt/deps/xwin LLVM_MINGW_BIN=/opt/deps/llvm-mingw/bin
cmake --preset linux-clangcl
cmake --build --preset linux-clangcl
file build/linux-clangcl/skse/plugin/LostArt.dll
# LostArt.dll: PE32+ executable (DLL) (GUI) x86-64, for MS Windows, ...
```

The first configure builds all vcpkg dependencies (Release only, triplet
`cmake/triplets/x64-windows-static-md-clangcl.cmake`); later configures restore them from
vcpkg's binary cache (`~/.cache/vcpkg/archives`, or `$VCPKG_DEFAULT_BINARY_CACHE`).
Timings measured on a 4-core container: see the table at the end.

### Hosts whose proxy blocks GitHub archive downloads

vcpkg downloads port sources as `https://github.com/<org>/<repo>/archive/<ref>.tar.gz` and some
upstream fixes as `https://github.com/<org>/<repo>/commit/<sha>.patch?full_index=1`. Some
sandboxed environments allow `git clone` from GitHub but block those URLs (HTTP 403).
`cmake/scripts/vcpkg-github-via-git.sh` is a vcpkg asset-cache script that re-creates both kinds
of download from a shallow git fetch, byte-identical to GitHub's (so vcpkg's SHA512 check still
passes):

```bash
export X_VCPKG_ASSET_SOURCES="x-script,$PWD/cmake/scripts/vcpkg-github-via-git.sh {url} {sha512} {dst}"
```

Archives always reproduce exactly (`git archive | gzip -n`); commit patches usually do
(`git format-patch --full-index --no-signature`), but not always: at the pinned baseline the
`spdlog` port's patch for gabime/spdlog@1685e69 does not. On such a host, copy that port from
vcpkg's registry cache into a directory outside the repo, point its two
`vcpkg_download_distfile` patches at local `git format-patch` output, and add the directory with
`export VCPKG_OVERLAY_PORTS=<dir>`. Normal hosts and CI need none of this.

### Caveat

A clean cross build proves the toolchain, not runtime correctness. clang-cl targets the MSVC
ABI, but always test a Linux-built DLL in game; release builds come from the `windows-msvc` CI job.

## Windows: MSVC (`windows-msvc`)

Needs: Visual Studio 2022 (17.10 or newer) with "Desktop development with C++", CMake ≥ 3.25,
Ninja (bundled with VS), git, and vcpkg.

From an **x64 Native Tools Command Prompt for VS 2022** (or after `ilammy/msvc-dev-cmd` in CI):

```bat
git clone https://github.com/microsoft/vcpkg C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat -disableMetrics
set VCPKG_ROOT=C:\vcpkg

cmake --preset windows-msvc
cmake --build --preset windows-msvc
ctest --preset windows-msvc
```

The DLL is `build\windows-msvc\skse\plugin\LostArt.dll`. The stock
`x64-windows-static-md` triplet is used (static libraries, dynamic CRT), and DirectXTK's shaders
are compiled by the Windows SDK's own `fxc.exe`, so no overlay port is involved.

## Installing into a game / MO2 profile

```bash
cmake --install build/linux-clangcl --prefix "<Skyrim>/Data"     # or build/windows-msvc
```

installs `SKSE/Plugins/LostArt.dll` and `LostArt.pdb`. The plugin needs SKSE and Address Library
for SKSE Plugins; it logs to `Documents/My Games/Skyrim Special Edition/SKSE/LostArt.log`.

## CI

`.github/workflows/ci.yml` runs:

- **linux**: `linux-tests` configure/build/ctest, then `python tools/validate_data.py` and
  `python tools/check_formkeys.py` when present (the latter with a shallow clone of
  Mutagen-Modding/Mutagen.Bethesda.FormKeys at `$FORMKEYS_DIR`).
- **windows**: `windows-msvc` configure/build/ctest; uploads `LostArt.dll` + `.pdb` as the
  `LostArt-dll` artifact. vcpkg binaries are cached between runs.
- **generator**: `dotnet build tools/Generator -c Release`.

## Files

| Path | Purpose |
| --- | --- |
| `CMakeLists.txt` | top level: options, global MSVC/clang-cl flags, components |
| `CMakePresets.json` | the three presets |
| `vcpkg.json`, `vcpkg-configuration.json` | vcpkg manifest + pinned registry baseline |
| `cmake/LostArtCommonLib.cmake` | fetches and configures CommonLibSSE-NG |
| `cmake/LostArtDependencies.cmake` | nlohmann_json / Catch2 (find_package → FetchContent) |
| `cmake/toolchains/linux-clangcl.cmake` | clang-cl + xwin cross toolchain |
| `cmake/triplets/x64-windows-static-md-clangcl.cmake` | vcpkg triplet for the cross build |
| `cmake/ports/directxtk/` | overlay port: DirectXTK shaders via fxc2 under Wine (Linux host only) |
| `cmake/scripts/vcpkg-github-via-git.sh` | optional asset-cache script (see above) |
| `cmake/version.rc.in` | version resource embedded in LostArt.dll |
