#!/bin/sh
# Runs the CLI on each expectation set and diffs the output.
#   tools/golden.sh [BUILD_DIR]        (default build/mac)
# A set is $HDTRANS_GOLDEN/<name>.in with <name>.expected. <name>.raw.in runs with -raw,
# <name>.each.in with -each (a session per line), any other set as one session.
# Both HDTRANS_GOLDEN and HDTRANS_DICTS must be explicitly configured.
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
[ -n "${HDTRANS_GOLDEN:-}" ] || { echo "HDTRANS_GOLDEN is not set; configure the expectation folder" >&2; exit 2; }
[ -n "${HDTRANS_DICTS:-}" ] || { echo "HDTRANS_DICTS is not set; configure the dictionary folder" >&2; exit 2; }
golden=$HDTRANS_GOLDEN
dicts=$HDTRANS_DICTS
[ -d "$golden" ] || { echo "no expectation folder at $golden" >&2; exit 2; }
[ -d "$dicts" ] || { echo "no dictionary folder at $dicts" >&2; exit 2; }
cli=${1:-$here/build/mac}/hdtrans
[ -x "$cli" ] || { echo "no CLI at $cli" >&2; exit 2; }
run=$(mktemp -d "$(dirname "$cli")/golden-run.XXXXXX")
trap 'rm -rf "$run"' EXIT
trap 'exit 1' HUP INT TERM
case ${HDTRANS_CACHE_DIR-unset} in
    ''|off) ;;
    *) mkdir "$run/cache"
       HDTRANS_CACHE_DIR=$run/cache ;;
esac
export HDTRANS_CACHE_DIR
case ${HDTRANS_SNAPSHOT-} in ''|off) ;; *) unset HDTRANS_SNAPSHOT ;; esac
unset HDTRANS_VERIFY_DICTS
status=0
for in in "$golden"/*.in; do
    [ -f "$in" ] || { echo "no sets in $golden" >&2; exit 2; }
    name=${in##*/}; name=${name%.in}
    flag=""
    case $name in *.raw) flag=-raw ;; *.each) flag=-each ;; esac
    "$cli" -reference -dicts "$dicts" $flag -in "$in" > "$run/$name.actual" || status=1
    n=$(diff "$golden/$name.expected" "$run/$name.actual" | grep -c '^<' || true)
    echo "$name: $n differing lines of $(wc -l < "$golden/$name.expected")"
    [ "$n" -eq 0 ] || status=1
done
exit $status
