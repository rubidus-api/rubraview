#!/bin/sh
# Builds the release layout under build/dist/ (RV-083, RFC-0001 §11.2 M9).
#
# What ships is deliberately short: one executable, the manual, the
# licences of the three vendored libraries, and the changelog. There is
# no installer and no runtime to place beside it — that is the point of
# the "no DLL beside it" criterion.
set -eu

out=build/dist

# The version comes from the header, the same place the Makefile reads
# it, so the folder and the executable inside it cannot disagree.
version=${1:-$(sed -n 's/^#define RUBRAVIEW_VERSION_STRING "\(.*\)".*/\1/p' include/rubraview/version.h)}
exe="rubraview-v$version.exe"

if [ ! -f "dist/$exe" ]; then
  printf '%s\n' "package: dist/$exe is missing — run 'make win64' first" >&2
  exit 1
fi

rm -rf "$out"
mkdir -p "$out" "$out/licences"

cp "dist/$exe" "$out/"
cp docs/manual/rubraview-manual.md "$out/manual.md"
cp THIRD_PARTY_NOTICES.md "$out/"
cp CHANGELOG.md "$out/"
[ -f LICENSE ] && cp LICENSE "$out/" || true

cp vendor/miniz/LICENSE "$out/licences/miniz-LICENSE.txt"
cp vendor/lzma/LICENSE.txt "$out/licences/lzma-sdk-LICENSE.txt"
cp vendor/libjpeg-turbo/LICENSE.md "$out/licences/libjpeg-turbo-LICENSE.md"
cp vendor/libjpeg-turbo/README.ijg "$out/licences/libjpeg-turbo-README.ijg"

printf '%s\n' "$version" > "$out/VERSION"

# The check the "no DLL beside it" criterion really needs: list what the
# executable imports, so a new dependency cannot slip in unnoticed.
if command -v x86_64-w64-mingw32-objdump >/dev/null 2>&1; then
  x86_64-w64-mingw32-objdump -p "dist/$exe" \
    | sed -n 's/^\tDLL Name: //p' | sort > "$out/imports.txt"
  printf '%s\n' "package: imports recorded in $out/imports.txt"
fi

printf '%s\n' "package: $out ready (version $version)"
ls -1 "$out"
