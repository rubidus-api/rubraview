#!/bin/sh
# Builds the release layout under dist/rubraview-v<version>/ (RV-083,
# RFC-0001 §11.2 M9).
#
# What ships is deliberately short: one executable, the manual, the
# licences of the three vendored libraries, and the changelog. There is
# no installer and no runtime to place beside it — that is the point of
# the "no DLL beside it" criterion.
set -eu

# The version comes from the header, the same place the Makefile reads
# it, so the folder and the executable inside it cannot disagree.
version=${1:-$(sed -n 's/^#define RUBRAVIEW_VERSION_STRING "\(.*\)".*/\1/p' include/rubraview/version.h)}
exe="rubraview-v$version.exe"

# Everything a build produces lives under dist/ (owner, 2026-09-11).
# The bundle is a folder named for the version, so it can be zipped and
# sent as it stands, and two versions can sit side by side without one
# overwriting the other.
out="dist/rubraview-v$version"

if [ ! -f "dist/$exe" ]; then
  printf '%s\n' "package: dist/$exe is missing — run 'make win64' first" >&2
  exit 1
fi

rm -rf "$out"
mkdir -p "$out" "$out/licences"

# Inside the bundle the program is plain `rubraview.exe`: the folder and
# the zip carry the version (owner, 2026-09-29), so a reader's shortcut
# and "open with" survive an update. Only what a reader needs goes in: the
# codec probe and gpu-check.cmd are developer tools, left in tools/.
cp "dist/$exe" "$out/rubraview.exe"
# The same executable on its own, for the release's direct download.
cp "dist/$exe" "dist/rubraview.exe"

cp docs/manual/rubraview-manual.md "$out/manual.md"
cp THIRD_PARTY_NOTICES.md "$out/"
cp CHANGELOG.md "$out/"
[ -f LICENSE ] && cp LICENSE "$out/" || true

cp vendor/proven/LICENSE "$out/licences/proven_c_lib-LICENSE.txt"
cp vendor/miniz/LICENSE "$out/licences/miniz-LICENSE.txt"
cp vendor/lzma/LICENSE.txt "$out/licences/lzma-sdk-LICENSE.txt"
cp vendor/libjpeg-turbo/LICENSE.md "$out/licences/libjpeg-turbo-LICENSE.md"
cp vendor/libjpeg-turbo/README.ijg "$out/licences/libjpeg-turbo-README.ijg"


# The check the "no DLL beside it" criterion really needs: list what the
# executable imports, so a new dependency cannot slip in unnoticed.
if command -v x86_64-w64-mingw32-objdump >/dev/null 2>&1; then
  x86_64-w64-mingw32-objdump -p "dist/$exe" \
    | sed -n 's/^\tDLL Name: //p' | sort > "dist/rubraview-v$version.imports.txt"
  printf '%s\n' "package: imports recorded in dist/rubraview-v$version.imports.txt (beside the bundle, not in it)"
fi

printf '%s\n' "package: $out ready (version $version)"
# The zip, made here so what is published is exactly this folder.
( cd dist && rm -f "rubraview-v$version.zip" && \
  python3 -c "import os,sys,zipfile; d=sys.argv[1]; z=zipfile.ZipFile(d+'.zip','w',zipfile.ZIP_DEFLATED); [z.write(os.path.join(r,f)) for r,_,fs in os.walk(d) for f in sorted(fs)]; z.close()" "rubraview-v$version" )
printf '%s\n' "package: dist/rubraview-v$version.zip and dist/rubraview.exe are the release"
ls -1 "$out"
