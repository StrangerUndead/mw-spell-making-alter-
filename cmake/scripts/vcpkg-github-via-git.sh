#!/bin/sh
# vcpkg asset-cache "x-script" provider that re-creates GitHub downloads from a shallow git fetch,
# byte-identical to what GitHub serves (so vcpkg's SHA512 check still applies):
#   https://github.com/<org>/<repo>/archive/<ref>.tar.gz
#       = git archive --format=tar --prefix=<repo>-<ref>/ <ref> | gzip -n
#   https://github.com/<org>/<repo>/commit/<sha>.patch[?full_index=1]
#       = git format-patch -1 --no-signature [--full-index] --stdout <sha>
#
# Only needed on hosts whose egress proxy allows git clone/fetch of GitHub but blocks GitHub
# archive/codeload downloads (e.g. sandboxed agent containers). Normal hosts and CI don't need it.
#
# Usage (see docs/dev/BUILDING.md):
#   export X_VCPKG_ASSET_SOURCES="x-script,$PWD/cmake/scripts/vcpkg-github-via-git.sh {url} {sha512} {dst}"
#
# Exit status non-zero => vcpkg falls back to the next source (normally the origin URL).
set -eu

url="$1"
sha512="$2"
dst="$3"

case "$url" in
    https://github.com/*/archive/*.tar.gz) kind=archive ;;
    https://github.com/*/commit/*.patch|https://github.com/*/commit/*.patch\?*) kind=patch ;;
    *) echo "vcpkg-github-via-git: not a GitHub archive/commit-patch URL: $url" >&2; exit 1 ;;
esac

path="${url#https://github.com/}"          # org/repo/{archive/<ref>.tar.gz | commit/<sha>.patch?...}
org="${path%%/*}"; path="${path#*/}"
repo="${path%%/*}"; path="${path#*/}"

work=$(mktemp -d "${TMPDIR:-/tmp}/vcpkg-gh-archive.XXXXXX")
trap 'rm -rf "$work"' EXIT INT TERM
git init -q --bare "$work/repo.git"

if [ "$kind" = archive ]; then
    ref="${path#archive/}"; ref="${ref%.tar.gz}"
    ref="${ref#refs/tags/}"; ref="${ref#refs/heads/}"
    # GitHub's top-level directory: <repo>-<ref>, with a leading "v" dropped from tags like
    # v1.2.3 and "/" in branch names replaced by "-".
    dir_ref=$(printf '%s' "$ref" | sed -e 's#^v\([0-9]\)#\1#' -e 's#/#-#g')
    if ! git -C "$work/repo.git" fetch -q --depth 1 "https://github.com/$org/$repo.git" "$ref" 2>"$work/err"; then
        git -C "$work/repo.git" fetch -q --depth 1 "https://github.com/$org/$repo.git" "refs/tags/$ref" 2>>"$work/err" || {
            cat "$work/err" >&2; exit 1; }
    fi
    git -C "$work/repo.git" archive --format=tar --prefix="$repo-$dir_ref/" FETCH_HEAD | gzip -n > "$work/out"
else
    sha="${path#commit/}"; query=""
    case "$sha" in *\?*) query="${sha#*\?}"; sha="${sha%%\?*}" ;; esac
    sha="${sha%.patch}"
    full_index=""
    case "&$query&" in *"&full_index=1&"*) full_index="--full-index" ;; esac
    # depth 2: the parent is needed to diff against
    git -C "$work/repo.git" fetch -q --depth 2 "https://github.com/$org/$repo.git" "$sha" 2>"$work/err" || {
        cat "$work/err" >&2; exit 1; }
    git -C "$work/repo.git" format-patch -1 --no-signature $full_index --stdout "$sha" > "$work/out"
    ref="$sha"
fi

actual=$(sha512sum "$work/out" | cut -d' ' -f1)
if [ -n "$sha512" ] && [ "$actual" != "$sha512" ]; then
    echo "vcpkg-github-via-git: $org/$repo@$ref re-created download hash $actual != expected $sha512" >&2
    exit 1
fi

mkdir -p "$(dirname "$dst")"
mv "$work/out" "$dst"
