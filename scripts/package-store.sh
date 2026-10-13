#!/bin/sh
# Builds dist/rubraview-v<version>-store.msix, the package uploaded to the
# Microsoft Store (D-122), with rubrapack from the folder scripts/package.sh
# made (run `make package` first).
#
# The package's identity is Partner Center's (Product identity), read from
# the file RUBRAVIEW_STORE_IDENTITY names: three lines, a name, a tab, the
# value -
#   Package/Identity/Name
#   Package/Identity/Publisher
#   Package/Properties/PublisherDisplayName
# The values are not kept in the repository and are not printed here.
#
# The Store refuses a version whose first part is 0, and keeps the fourth
# for itself: the package's version is the program's with the first part
# one higher (0.0.70 is 1.0.70.0 in the Store).
set -eu

version=${1:-$(sed -n 's/^#define RUBRAVIEW_VERSION_STRING "\(.*\)".*/\1/p' include/rubraview/version.h)}
stage="dist/rubraview-v$version"
out="dist/rubraview-v$version-store.msix"
rubrapack=${RUBRAPACK:-rubrapack}

fail() { printf '%s\n' "package-store: $1" >&2; exit 1; }

[ -f "$stage/rubraview.exe" ] || fail "$stage is missing - run 'make package' first"
command -v "$rubrapack" >/dev/null 2>&1 || fail "no rubrapack (set RUBRAPACK)"
[ -n "${RUBRAVIEW_STORE_IDENTITY:-}" ] || fail "RUBRAVIEW_STORE_IDENTITY names no file"
[ -r "$RUBRAVIEW_STORE_IDENTITY" ] || fail "the file RUBRAVIEW_STORE_IDENTITY names cannot be read"

field() {
  tr -d '\r' < "$RUBRAVIEW_STORE_IDENTITY" | awk -F '\t' -v key="$1" '$1 == key { print $2; exit }'
}
name=$(field Package/Identity/Name)
publisher=$(field Package/Identity/Publisher)
publisher_name=$(field Package/Properties/PublisherDisplayName)
[ -n "$name" ] && [ -n "$publisher" ] && [ -n "$publisher_name" ] \
  || fail "the identity file lacks one of the three values"

major=${version%%.*}
store_version="$((major + 1)).${version#*.}"

python3 scripts/check-store.py
rm -f "$out"
"$rubrapack" build resources/distribution/rubraview-store.toml -o "$out" \
    -D "VERSION=$store_version" -D "STAGE=../../$stage" \
    -D "STORE_NAME=$name" -D "STORE_PUBLISHER=$publisher" -D "STORE_PUBLISHER_NAME=$publisher_name"
"$rubrapack" lint "$out"
printf '%s\n' "package-store: $out (version $store_version.0 in the Store; not signed - the Store signs it)"
