#!/usr/bin/env bash
# Compile Lost Art of Spellmaking's Papyrus scripts on Linux (or anywhere with bash).
#
#   scripts/build.sh                 build Caprica if needed, compile, verify
#   scripts/build.sh --check         compile into a temp dir and verify only (CI)
#   CAPRICA=/path/to/Caprica scripts/build.sh     use an existing Caprica binary
#                                                 (Caprica.exe on Windows works too)
#   PAPYRUS_IMPORTS="dir1:dir2" scripts/build.sh  compile against real headers
#                                                 (SKSE, SkyUI SDK, MCM Helper
#                                                 Scripts/Source) before the stubs
#
# Output: build/papyrus/*.pex (one per scripts/Source/*.psc). scripts/Stubs/ is only an
# import path for compile-checking and is never compiled or shipped.
#
# Compiler: Caprica (MIT), nikitalita's cross-platform branch "os-independent" at a
# pinned commit, plus scripts/caprica/caprica-linux.patch (build fixes for GCC/Clang
# on Linux, a getlogin() fallback for headless shells, and Skyrim-mode acceptance of
# native functions in non-native scripts, which the Creation Kit compiler allows and
# every SKSE plugin script relies on). Needs cmake, ninja or make, a C++23 compiler
# (clang++ 16+ or g++ 13+) and the Boost (filesystem, program_options, container),
# fmt and pugixml development packages:
#   apt-get install -y cmake ninja-build clang libboost-filesystem-dev \
#       libboost-program-options-dev libboost-container-dev libfmt-dev libpugixml-dev
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/scripts/Source"
STUBS="$ROOT/scripts/Stubs"
FLAGS="$STUBS/TESV_Papyrus_Flags.flg"
OUT="$ROOT/build/papyrus"
TOOLS="$ROOT/build/tools/caprica"

CAPRICA_REPO="https://github.com/nikitalita/Caprica.git"
CAPRICA_COMMIT="932705b74bd4380f2ead4f49455980856742b399"   # branch os-independent, 2023-10-16
CAPRICA_PATCH="$ROOT/scripts/caprica/caprica-linux.patch"

check_only=0
for arg in "$@"; do
  case "$arg" in
    --check) check_only=1 ;;
    -h|--help) sed -n '2,25p' "${BASH_SOURCE[0]}"; exit 0 ;;
    *) echo "unknown argument: $arg" >&2; exit 2 ;;
  esac
done

log() { printf '[papyrus] %s\n' "$*"; }

build_caprica() {
  local src="$TOOLS/src" bld="$TOOLS/build"
  if [[ ! -d "$src/.git" ]]; then
    log "fetching Caprica $CAPRICA_COMMIT"
    rm -rf "$src"
    git clone --quiet "$CAPRICA_REPO" "$src"
  fi
  git -C "$src" fetch --quiet origin "$CAPRICA_COMMIT" 2>/dev/null || true
  git -C "$src" checkout --quiet --force "$CAPRICA_COMMIT"
  git -C "$src" reset --quiet --hard
  git -C "$src" apply "$CAPRICA_PATCH"
  local gen=() cxx=()
  command -v ninja >/dev/null && gen=(-G Ninja)
  command -v clang++ >/dev/null && cxx=(-DCMAKE_CXX_COMPILER=clang++)
  local arch_flags=""
  case "$(uname -m)" in x86_64|amd64) arch_flags="-msse4.2" ;; esac
  log "building Caprica (one-time)"
  cmake -S "$src" -B "$bld" "${gen[@]}" "${cxx[@]}" \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="$arch_flags" >/dev/null
  cmake --build "$bld" --parallel >/dev/null
  mkdir -p "$TOOLS/bin"
  cp "$bld/Caprica/Caprica" "$TOOLS/bin/Caprica"
  git -C "$src" rev-parse HEAD > "$TOOLS/bin/COMMIT"
  sha256sum "$CAPRICA_PATCH" | cut -d' ' -f1 > "$TOOLS/bin/PATCH_SHA256"
}

if [[ -z "${CAPRICA:-}" ]]; then
  CAPRICA="$TOOLS/bin/Caprica"
  want_patch="$(sha256sum "$CAPRICA_PATCH" | cut -d' ' -f1)"
  if [[ ! -x "$CAPRICA" ]] \
     || [[ "$(cat "$TOOLS/bin/COMMIT" 2>/dev/null)" != "$CAPRICA_COMMIT" ]] \
     || [[ "$(cat "$TOOLS/bin/PATCH_SHA256" 2>/dev/null)" != "$want_patch" ]]; then
    build_caprica
  fi
fi

if [[ $check_only -eq 1 ]]; then
  OUT="$(mktemp -d)"
  trap 'rm -rf "$OUT"' EXIT
fi
mkdir -p "$OUT"

imports=()
if [[ -n "${PAPYRUS_IMPORTS:-}" ]]; then
  IFS=':' read -r -a extra <<< "$PAPYRUS_IMPORTS"
  for d in "${extra[@]}"; do imports+=(--import "$d"); done
fi
imports+=(--import "$SRC" --import "$STUBS")

mapfile -t sources < <(cd "$SRC" && ls -1 *.psc | sort)
log "compiling ${#sources[@]} scripts: ${sources[*]}"
# Run from Source with relative names so the .pex records "LostArt.psc", not a
# build-machine path; --anonymize blanks the user and machine names.
(
  cd "$SRC"
  "$CAPRICA" --game skyrim --ignorecwd --anonymize --quiet \
    "${imports[@]}" --flags "$FLAGS" --output "$OUT" "${sources[@]}"
)

missing=0
for s in "${sources[@]}"; do
  pex="$OUT/${s%.psc}.pex"
  if [[ ! -s "$pex" ]]; then echo "missing output: $pex" >&2; missing=1; fi
done
[[ $missing -eq 0 ]] || exit 1

log "verifying .pex structure"
python3 "$ROOT/scripts/pexinfo.py" "$OUT"/*.pex >/dev/null
python3 "$ROOT/scripts/pexinfo.py" --check-api "$OUT/LostArt.pex" | tail -n 1

if [[ $check_only -eq 1 ]]; then
  log "OK (check only)"
else
  log "OK -> $OUT"
fi
