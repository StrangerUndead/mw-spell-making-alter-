#!/usr/bin/env bash
# Serialize the generated plugins to Spriggit YAML (plugin/LostArt, plugin/LostArt_Slots) and
# run the round-trip check: YAML -> deserialize -> compare with the generated plugin.
#
#   tools/Generator/spriggit.sh [plugins-dir=build/plugins] [yaml-root=plugin]
#   tools/Generator/spriggit.sh --check-only [plugins-dir] [yaml-root]   (no re-serialize)
#
# Needs: .NET 10 runtime (Spriggit.CLI 0.41 targets net10.0) and `dotnet tool install --global
# Spriggit.CLI --version 0.41.0`. Spriggit downloads Spriggit.Yaml.Skyrim 0.41.0 from NuGet.
set -euo pipefail

CHECK_ONLY=0
if [[ "${1:-}" == "--check-only" ]]; then CHECK_ONLY=1; shift; fi
PLUGINS="${1:-build/plugins}"
YAML="${2:-plugin}"
SPRIGGIT_VERSION="${SPRIGGIT_VERSION:-0.41.0}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [[ -z "${DOTNET_ROOT:-}" ]]; then
  DOTNET_ROOT="$(dirname "$(readlink -f "$(command -v dotnet)")")"
  export DOTNET_ROOT
fi
SPRIGGIT="$(command -v spriggit || true)"
[[ -z "$SPRIGGIT" && -x "$HOME/.dotnet/tools/spriggit" ]] && SPRIGGIT="$HOME/.dotnet/tools/spriggit"
if [[ -z "$SPRIGGIT" ]]; then
  echo "spriggit not found: dotnet tool install --global Spriggit.CLI --version $SPRIGGIT_VERSION" >&2
  exit 2
fi

TMP="$(mktemp -d)"
STAGE=""
if [[ -d /dev/shm ]] && [[ "$(stat -f -c %T /dev/shm 2>/dev/null)" == "tmpfs" ]]; then STAGE="$(mktemp -d -p /dev/shm)"; fi
trap 'rm -rf "$TMP" ${STAGE:+"$STAGE"}' EXIT
status=0
dotnet build "$HERE" -v q -nologo >/dev/null

for p in LostArt LostArt_Slots; do
  esp="$PLUGINS/$p.esp"
  [[ -f "$esp" ]] || { echo "missing $esp (run the generator first)" >&2; exit 2; }
  if [[ $CHECK_ONLY -eq 0 ]]; then
    # Serialize into a fresh folder, then swap it in, so records removed from data disappear.
    "$SPRIGGIT" serialize -i "$esp" -o "$TMP/yaml/$p" -g SkyrimSE -p Spriggit.Yaml.Skyrim -v "$SPRIGGIT_VERSION" -u >"$TMP/$p.serialize.log" 2>&1 \
      || { cat "$TMP/$p.serialize.log" >&2; exit 1; }
    rm -rf "$YAML/$p"
    mkdir -p "$YAML"
    cp -r "$TMP/yaml/$p" "$YAML/$p"
  fi
  # Spriggit deserializes records in directory-enumeration order. Stage the YAML on tmpfs in
  # alphabetical order (what NTFS gives on Windows) so the check can be byte-exact on Linux too.
  src="$YAML/$p"
  if [[ -n "$STAGE" ]] && python3 "$HERE/stage_sorted.py" "$YAML/$p" "$STAGE/$p"; then src="$STAGE/$p"; fi
  "$SPRIGGIT" deserialize -i "$src" -o "$TMP/rt/$p.esp" >"$TMP/$p.deserialize.log" 2>&1 \
    || { cat "$TMP/$p.deserialize.log" >&2; exit 1; }
  echo -n "round-trip $p.esp: "
  dotnet run --project "$HERE" --no-build -- compare "$esp" "$TMP/rt/$p.esp" || status=1
done
exit $status
