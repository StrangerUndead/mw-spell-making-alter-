#!/usr/bin/env bash
# Builds interface/build/LostArt_Spellmaking.swf headlessly (no Flash IDE) and verifies it.
#
#   ./build.sh             compile (MTASC, strict) + structural check + translation-key check
#   ./build.sh --ffdec     ... + JPEXS FFDec dump/decompile check (needs java)
#   ./build.sh --harness   ... + offline Ruffle/Playwright harness (needs node + playwright)
#   ./build.sh --all       everything
#   ./build.sh --clean     remove build/ and test/out/
#
# Tools are fetched once into interface/.tools/ (git-ignored) and pinned by SHA-256:
#   MTASC 1.14 (GPL compiler; output is not affected by its licence) from Ubuntu's archive
#   JPEXS FFDec 26.3.0 (GPL) from GitHub releases     - only for --ffdec
#   @ruffle-rs/ruffle 0.6.0 (MIT/Apache) from npm      - only for --harness
# Override with MTASC=/path/to/mtasc MTASC_STD=/path/to/std (dir containing std/ and std8/).
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOLS="${LA_TOOLS_DIR:-$HERE/.tools}"
OUT="$HERE/build/LostArt_Spellmaking.swf"

MTASC_DEB_URL="http://archive.ubuntu.com/ubuntu/pool/universe/m/mtasc/mtasc_1.14-3build5_amd64.deb"
MTASC_DEB_SHA256="c563e8670078bb508ae25e5cfe826e01f9c6fdd6d4587f0502e2f97d05448714"
FFDEC_VERSION="26.3.0"
FFDEC_URL="https://github.com/jindrapetrik/jpexs-decompiler/releases/download/version${FFDEC_VERSION}/ffdec_${FFDEC_VERSION}.zip"
FFDEC_SHA256="35f4930eb7c380afe66f2117f90b006deac0631473ad7500bb39c78f68645ecd"
RUFFLE_VERSION="0.6.0"

# Stage: 1280x720 like the vanilla menus (the menu lays itself out from Stage.visibleRect and
# sets noScale, so this only matters to tools). 30 fps, black (the DLL loads it with bg alpha 0).
HEADER="1280:720:30:000000"

do_ffdec=0; do_harness=0
for a in "$@"; do
  case "$a" in
    --ffdec) do_ffdec=1 ;;
    --harness) do_harness=1 ;;
    --all) do_ffdec=1; do_harness=1 ;;
    --clean) rm -rf "$HERE/build" "$HERE/test/out"; echo "cleaned"; exit 0 ;;
    -h|--help) sed -n '2,16p' "$0"; exit 0 ;;
    *) echo "unknown option $a" >&2; exit 2 ;;
  esac
done

log() { printf '\n== %s\n' "$*"; }

fetch() { # url sha256 dest
  local url="$1" sha="$2" dest="$3"
  if [[ -f "$dest" ]] && echo "$sha  $dest" | sha256sum -c --status; then return 0; fi
  echo "downloading $url"
  curl -fsSL --retry 3 -o "$dest.part" "$url"
  echo "$sha  $dest.part" | sha256sum -c --status || { echo "SHA-256 mismatch for $url" >&2; rm -f "$dest.part"; exit 1; }
  mv "$dest.part" "$dest"
}

ensure_mtasc() {
  if [[ -n "${MTASC:-}" ]]; then
    MTASC_STD="${MTASC_STD:-$(dirname "$(dirname "$MTASC")")/share/mtasc}"
    return
  fi
  if command -v mtasc >/dev/null 2>&1 && [[ -d /usr/share/mtasc/std ]]; then
    MTASC="$(command -v mtasc)"; MTASC_STD=/usr/share/mtasc; return
  fi
  mkdir -p "$TOOLS"
  local deb="$TOOLS/mtasc_1.14-3build5_amd64.deb"
  if [[ ! -x "$TOOLS/mtasc/usr/bin/mtasc" ]]; then
    fetch "$MTASC_DEB_URL" "$MTASC_DEB_SHA256" "$deb"
    rm -rf "$TOOLS/mtasc"; mkdir -p "$TOOLS/mtasc"
    if command -v dpkg-deb >/dev/null 2>&1; then
      dpkg-deb -x "$deb" "$TOOLS/mtasc"
    else  # plain ar + tar fallback
      (cd "$TOOLS/mtasc" && ar x "$deb" && tar -xf data.tar.* && rm -f data.tar.* control.tar.* debian-binary)
    fi
  fi
  MTASC="$TOOLS/mtasc/usr/bin/mtasc"
  MTASC_STD="$TOOLS/mtasc/usr/share/mtasc"
}

ensure_ffdec() {
  mkdir -p "$TOOLS"
  if [[ ! -f "$TOOLS/ffdec/ffdec.jar" ]]; then
    fetch "$FFDEC_URL" "$FFDEC_SHA256" "$TOOLS/ffdec_${FFDEC_VERSION}.zip"
    rm -rf "$TOOLS/ffdec"; mkdir -p "$TOOLS/ffdec"
    (cd "$TOOLS/ffdec" && unzip -q "../ffdec_${FFDEC_VERSION}.zip")
  fi
  FFDEC_JAR="$TOOLS/ffdec/ffdec.jar"
}

# ---------------------------------------------------------------------------------------------
log "compile (MTASC, -strict, SWF 8)"
ensure_mtasc
mkdir -p "$HERE/build"
"$MTASC" 2>&1 | head -1 || true
# Every class file is passed explicitly so unused classes are type-checked too.
mapfile -t CLASSES < <(cd "$HERE/src" && find . -name '*.as' ! -name 'skse.as' ! -name 'LostArtMain.as' | sed 's|^\./||' | sort)
(cd "$HERE/src" && "$MTASC" -version 8 -strict -wimp \
  -cp "$MTASC_STD/std" -cp "$MTASC_STD/std8" -cp . \
  -swf "$OUT" -header "$HEADER" -main LostArtMain.as "${CLASSES[@]}")
echo "wrote $OUT ($(stat -c %s "$OUT") bytes)"

log "structural check (test/swfcheck.py)"
python3 "$HERE/test/swfcheck.py" "$OUT"

log "translation keys"
python3 - "$HERE" <<'PY'
import io, os, re, sys
here = sys.argv[1]
used = set()
for d, _, files in os.walk(os.path.join(here, "src")):
    for f in files:
        if f.endswith(".as"):
            src = open(os.path.join(d, f), encoding="utf-8").read()
            src = re.sub(r"/\*.*?\*/", "", src, flags=re.S)
            src = re.sub(r"//[^\n]*", "", src)
            used |= set(re.findall(r'"(\$LA_[A-Za-z0-9_]+)"', src))
def keys(path, enc):
    out = set()
    for line in io.open(path, encoding=enc):
        line = line.lstrip("﻿")
        if line.startswith("$LA_") and "\t" in line:
            out.add(line.split("\t", 1)[0])
    return out
frag = keys(os.path.join(here, "translations", "LostArt_UI_ENGLISH.txt"), "utf-8")
family = re.compile(r"^\$LA_(Unit|Range|School|Rank)_")
missing = sorted(k for k in used if k not in frag and not family.match(k))
print("keys used by the SWF: %d, in fragment: %d" % (len(used), len(frag)))
if missing:
    print("FAIL: keys missing from translations/LostArt_UI_ENGLISH.txt: " + ", ".join(missing))
    sys.exit(1)
main = os.path.join(here, "..", "data", "translations", "LostArt_ENGLISH.txt")
if os.path.exists(main):
    have = keys(main, "utf-16")
    todo = sorted((used | frag) - have)
    print("data/translations/LostArt_ENGLISH.txt: %s" % ("all menu keys present" if not todo else
          "WARNING %d menu keys not merged yet: %s" % (len(todo), ", ".join(todo))))
else:
    print("data/translations/LostArt_ENGLISH.txt not present yet (merge the fragment when it is)")
PY

if [[ $do_ffdec == 1 ]]; then
  log "FFDec verification"
  command -v java >/dev/null || { echo "java not found" >&2; exit 1; }
  ensure_ffdec
  DUMP="$HERE/build/ffdec_dump.txt"
  java -jar "$FFDEC_JAR" -dumpSWF "$OUT" > "$DUMP" 2>/dev/null
  echo "dumpSWF: $(grep -c 'tagId=' "$DUMP") tags ($DUMP)"
  rm -rf "$HERE/build/decompiled"
  java -jar "$FFDEC_JAR" -export script "$HERE/build/decompiled" "$OUT" >/dev/null 2>&1
  n=$(find "$HERE/build/decompiled" -name '*.as' | wc -l)
  echo "decompiled AS2 scripts: $n ($HERE/build/decompiled)"
  grep -rq "LA_SetState" "$HERE/build/decompiled" || { echo "FAIL: LA_SetState not found in decompiled code" >&2; exit 1; }
  [[ $n -ge 28 ]] || { echo "FAIL: expected >= 28 decompiled scripts" >&2; exit 1; }
  echo "FFDec: OK"
fi

if [[ $do_harness == 1 ]]; then
  log "offline harness (Ruffle + Playwright)"
  mkdir -p "$TOOLS"
  if [[ ! -f "$TOOLS/node_modules/@ruffle-rs/ruffle/ruffle.js" ]]; then
    [[ -f "$TOOLS/package.json" ]] || echo '{"name":"lostart-interface-tools","private":true}' > "$TOOLS/package.json"
    (cd "$TOOLS" && npm install --no-audit --no-fund "@ruffle-rs/ruffle@${RUFFLE_VERSION}")
  fi
  node "$HERE/test/harness/run.mjs"
fi

log "done: $OUT"
