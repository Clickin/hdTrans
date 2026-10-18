#!/bin/sh
# Runs the CLI on each expectation set and diffs the output.
#   tools/golden.sh [BUILD_DIR]        (default build/mac)
# A set is $HDTRANS_GOLDEN/<name>.in with <name>.expected. <name>.raw.in runs with -raw,
# <name>.each.in with -each (a session per line), any other set as one session.
# Dictionaries: $HDTRANS_DICTS (default ../dicts/base).
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
golden=${HDTRANS_GOLDEN:-$here/../hdtrans-golden}
dicts=${HDTRANS_DICTS:-$here/../dicts/base}
cli=${1:-$here/build/mac}/hdtrans
[ -x "$cli" ] || { echo "no CLI at $cli" >&2; exit 2; }
status=0
for in in "$golden"/*.in; do
    [ -f "$in" ] || { echo "no sets in $golden" >&2; exit 2; }
    name=${in##*/}; name=${name%.in}
    flag=""
    case $name in *.raw) flag=-raw ;; *.each) flag=-each ;; esac
    "$cli" -dicts "$dicts" $flag -in "$in" > "$golden/$name.actual" || status=1
    n=$(diff "$golden/$name.expected" "$golden/$name.actual" | grep -c '^<' || true)
    echo "$name: $n differing lines of $(wc -l < "$golden/$name.expected")"
    [ "$n" -eq 0 ] || status=1
done
exit $status
