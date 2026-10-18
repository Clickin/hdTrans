#!/bin/sh
# Check comparison failures with a small subset of the configured expectation set.
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
build=${1:-$here/build/mac}
: "${HDTRANS_GOLDEN:?configure the expectation folder}"
: "${HDTRANS_DICTS:?configure the dictionary folder}"
tmp=$(mktemp -d "${TMPDIR:-/tmp}/hdtrans-golden-check.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
trap 'exit 1' HUP INT TERM
mkdir "$tmp/sets"
head -n 2 "$HDTRANS_GOLDEN/corpus.raw.in" > "$tmp/sets/probe.raw.in"
head -n 2 "$HDTRANS_GOLDEN/corpus.raw.expected" > "$tmp/expected"
check() {
    want=$1
    status=0
    HDTRANS_GOLDEN=$tmp/sets sh "$here/tools/golden.sh" "$build" > "$tmp/log" 2>&1 || status=$?
    [ "$status" = "$want" ] || { cat "$tmp/log"; echo "unexpected exit: $status" >&2; exit 1; }
    [ ! -f "$tmp/sets/probe.raw.actual" ]
}
cp "$tmp/expected" "$tmp/sets/probe.raw.expected"
check 0
head -n 1 "$tmp/expected" > "$tmp/sets/probe.raw.expected"
check 1
: > "$tmp/sets/probe.raw.expected"
check 1
cp "$tmp/expected" "$tmp/sets/probe.raw.expected"
printf 'extra\n' >> "$tmp/sets/probe.raw.expected"
check 1
printf 'different\ndifferent\n' > "$tmp/sets/probe.raw.expected"
check 1
rm "$tmp/sets/probe.raw.expected"
check 2
cp "$tmp/expected" "$tmp/sets/probe.raw.expected"
rm "$tmp/sets/probe.raw.in"
check 2
echo 'golden comparison contract: PASS'
