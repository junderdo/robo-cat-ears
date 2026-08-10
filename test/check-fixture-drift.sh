#!/usr/bin/env bash
# Compare this repo's copy of the wire-format fixture against the canonical one.
#
# The canonical file is docs/spec/wire-format-fixture.json in
# github.com/junderdo/milk-lab-creations. This repo carries a byte-identical
# copy because a C test cannot reach across repositories at build time. Point
# MILKLAB_REPO at a checkout, or keep one beside this repo.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
copy="$here/wire-format-fixture.json"
relative_path="docs/spec/wire-format-fixture.json"

canonical="${MILKLAB_REPO:-$here/../../../milk-lab-creations}/$relative_path"

if [[ ! -f "$canonical" ]]; then
    echo "check-fixture-drift: no canonical fixture at $canonical" >&2
    echo "Set MILKLAB_REPO to a milk-lab-creations checkout. Refusing to claim the copies agree." >&2
    exit 1
fi

if ! diff -u "$canonical" "$copy"; then
    echo >&2
    echo "check-fixture-drift: this repo's fixture copy has drifted from $canonical" >&2
    echo "Copy the canonical file over test/wire-format-fixture.json and rerun the tests in both repos." >&2
    exit 1
fi

echo "check-fixture-drift: fixture matches $canonical"
