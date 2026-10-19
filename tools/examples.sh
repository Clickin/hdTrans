#!/bin/sh
# Translates each examples/<case>.ja.txt as one document into build/examples/<case>.ko.txt and
# diffs it against examples/<case>.ko.txt; the listing examples/list.txt must match the counts.
#   tools/examples.sh CLI DICTS [--accept]
# --accept copies the outputs over examples/*.ko.txt and refreshes the counts in list.txt.
# <case>.want.ko.txt is the correct translation of a case the engine still gets wrong; its lines
# that the output matches are counted and reported, never failed on. An empty DICTS skips (77).
set -eu
LC_ALL=C; export LC_ALL
[ $# -ge 2 ] || { echo "usage: tools/examples.sh CLI DICTS [--accept]" >&2; exit 2; }
cli=$1 dicts=$2 accept=${3-}
[ -n "$dicts" ] || { echo "HDTRANS_DICTS is not set; skipping examples"; exit 77; }
here=$(cd "$(dirname "$0")/.." && pwd)
src=$here/examples
out=$here/build/examples
mkdir -p "$out"
: > "$out/list.txt"
status=0 good=0 total=0
for ja in "$src"/*.ja.txt; do
    name=${ja##*/}; name=${name%.ja.txt}
    ko=$out/$name.ko.txt
    "$cli" -dicts "$dicts" -in "$ja" > "$ko" || { echo "$name: CLI failed" >&2; exit 1; }
    if [ "$accept" = --accept ]; then
        cp "$ko" "$src/$name.ko.txt"
    elif ! diff "$src/$name.ko.txt" "$ko"; then
        echo "$name: output differs from examples/$name.ko.txt"
        status=1
    fi
    want=$src/$name.want.ko.txt
    [ -f "$want" ] || want=$ko
    set -- $(awk 'NR == FNR { w[FNR] = $0; n = FNR; next } (FNR in w) && w[FNR] == $0 { ok++ }
        END { print ok + 0, n + 0 }' "$want" "$ko")
    if [ "$want" != "$ko" ]; then
        echo "want $name: $1/$2"
        good=$((good + $1)) total=$((total + $2))
    fi
    lines=$(awk 'END { print NR }' "$ja")
    awk -F '\t' -v name="$name" -v lines="$lines" -v ok="$1" -v n="$2" '
        $1 == name { d = $4 } END { printf "%s\t%s줄\twant %s/%s\t%s\n", name, lines, ok, n, d }
    ' "$src/list.txt" >> "$out/list.txt"
done
if [ "$accept" = --accept ]; then
    cp "$out/list.txt" "$src/list.txt"
elif ! diff "$src/list.txt" "$out/list.txt"; then
    echo "examples/list.txt is out of date (run with --accept)"
    status=1
fi
echo "want: $good/$total correct"
exit $status
