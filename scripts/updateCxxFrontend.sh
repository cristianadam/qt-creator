#!/bin/sh
# Re-imports src/libs/3rdparty/cxx-frontend/cxx from a checkout of
# https://github.com/robertoraggi/cplusplus.
#
# The vendored tree carries no local changes, so the import is a plain copy.
# What the script adds is the bookkeeping that is easy to forget: recording
# which upstream revision the snapshot came from, in the three places that
# state it, and reporting source files that appeared or disappeared, because
# those have to be added to CMakeLists.txt and cxx-frontend.qbs by hand.
#
# Usage: scripts/updateCxxFrontend.sh <path-to-cplusplus-checkout>

set -e

if [ $# -ne 1 ]; then
    echo "usage: $0 <path-to-cplusplus-checkout>" >&2
    exit 2
fi

upstream=$1
here=$(cd "$(dirname "$0")/.." && pwd)
vendored=$here/src/libs/3rdparty/cxx-frontend

if [ ! -d "$upstream/src/parser/cxx" ]; then
    echo "$upstream does not look like a cplusplus checkout" >&2
    exit 1
fi

revision=$(git -C "$upstream" rev-parse HEAD)
date=$(git -C "$upstream" log -1 --format=%ad --date=short)

before=$(cd "$vendored/cxx" && find . \( -name '*.cc' -o -name '*.h' \) | sort)

rm -rf "$vendored/cxx"
cp -R "$upstream/src/parser/cxx" "$vendored/cxx"
cp "$upstream/LICENSE" "$vendored/LICENSE"

after=$(cd "$vendored/cxx" && find . \( -name '*.cc' -o -name '*.h' \) | sort)

# The revision is recorded in the README and in the attribution entry that
# feeds the SBOM. Rewriting qt_attributions.json through the json module would
# reflow the whole file, so only the one line is touched.
perl -pi -e "s/^\| Revision \| .* \|\$/| Revision | $revision ($date) |/" \
    "$vendored/README.md"
python3 - "$here/qt_attributions.json" "$revision" <<'PY'
import re, sys

path, revision = sys.argv[1], sys.argv[2]
text = open(path).read()
updated, count = re.subn(
    r'("Id": "cxx-frontend",(?:.|\n)*?"Version": ")[0-9a-f]{40}(")',
    lambda m: m.group(1) + revision + m.group(2), text, count=1)
if count != 1:
    sys.exit("could not find the cxx-frontend Version in %s" % path)
open(path, "w").write(updated)
PY

echo "imported $revision ($date)"

if [ "$before" != "$after" ]; then
    echo
    echo "the set of source files changed; update CMakeLists.txt and cxx-frontend.qbs:"
    printf '%s\n' "$before" > /tmp/cxxfrontend-before.$$
    printf '%s\n' "$after" > /tmp/cxxfrontend-after.$$
    diff /tmp/cxxfrontend-before.$$ /tmp/cxxfrontend-after.$$ || true
    rm -f /tmp/cxxfrontend-before.$$ /tmp/cxxfrontend-after.$$
fi

echo
echo "now run tst_cxxfrontend and bring its known-failure list up to date."
