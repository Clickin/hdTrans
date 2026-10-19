#!/bin/sh
# Translates the chapters fetched by tools/fetch-syosetu.sh with two builds and reports the lines
# that differ.
#   tools/e2e.sh A B DICTS [CACHE]      (default CACHE: build/e2e)
# A and B are CLI binaries; A may instead be a tools/snapshot.sh run folder, from which the
# document set with the same input is taken. For each stub tests/e2e/<name>.ja.txt, the chapter
# <CACHE>/<name>.ja.txt is translated without its first line as one document with default
# settings; <CACHE>/<name>.ko.txt (B) and <CACHE>/base/<name>.ko.txt (A) repeat that line, then
# hold the output. The report gives each changed chapter's count and its changed lines (file
# line number, A, B), at most 20 per chapter and 200 in all. A chapter that is not cached, or not
# in the snapshot run, is listed as missing. HDTRANS_DICTS_A gives A its own dictionaries.
# Exit: 0 identical, 1 changed, 2 invalid arguments, failed CLI or no chapter compared.
set -eu
[ $# -eq 3 ] || [ $# -eq 4 ] || { echo 'usage: tools/e2e.sh <cliA|runA> <cliB> <dicts> [cache]' >&2; exit 2; }
here=$(cd "$(dirname "$0")/.." && pwd)
stubs=$here/tests/e2e
cache=${4:-$here/build/e2e}
a=$1 b=$2
for dir in "$3" "${HDTRANS_DICTS_A:-$3}"; do
    [ -d "$dir" ] || { echo "no dictionaries at $dir" >&2; exit 2; }
done
dictsB=$(cd "$3" && pwd)
dictsA=$(cd "${HDTRANS_DICTS_A:-$3}" && pwd)
[ -x "$b" ] && [ -f "$b" ] || { echo "no CLI at $b" >&2; exit 2; }
if [ -d "$a" ]; then
    [ -f "$a/sets.tsv" ] || { echo "not a snapshot run: $a" >&2; exit 2; }
elif [ ! -x "$a" ]; then
    echo "no CLI or snapshot run at $a" >&2; exit 2
fi
[ -d "$cache" ] || { echo "no chapter cache at $cache" >&2; exit 2; }
mkdir -p "$cache/base"
tmp=$(mktemp -d "${TMPDIR:-/tmp}/hdtrans-e2e.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
trap 'exit 2' HUP INT TERM
mkdir "$tmp/cacheA" "$tmp/cacheB"
export LC_ALL=C
unset HDTRANS_SNAPSHOT HDTRANS_VERIFY_DICTS HDTRANS_DRIVER_DUMP
sha() {
    if command -v sha256sum > /dev/null; then sha256sum "$1"; else shasum -a 256 "$1"; fi |
        awk '{print $1}'
}
# translate CLI DICTS CACHEDIR HEADER OUT: the body in $tmp/body.txt, as one document.
translate() {
    { printf '%s\n' "$4"; HDTRANS_CACHE_DIR=$3 "$1" -dicts "$2" -in "$tmp/body.txt"; } > "$5" ||
        { echo "$name: $1 failed" >&2; exit 2; }
}
chapters=0 changedSets=0 changedLines=0 printed=0 missing=
for stub in "$stubs"/*.ja.txt; do
    [ -f "$stub" ] || { echo "no stubs in $stubs" >&2; exit 2; }
    name=${stub##*/}; name=${name%.ja.txt}
    header=$(head -n 1 "$stub")
    ja=$cache/$name.ja.txt
    if [ ! -f "$ja" ] || [ "$(head -n 1 "$ja")" != "$header" ]; then
        missing="$missing $name"; continue
    fi
    tail -n +2 "$ja" > "$tmp/body.txt"
    if [ -d "$a" ]; then
        setName=$(awk -F '\t' -v sum="$(sha "$tmp/body.txt")" '
            NR > 1 && $2 == "document" && $5 == sum {print $1; exit}' "$a/sets.tsv")
        if [ -z "$setName" ] || [ ! -f "$a/outputs/$setName.out" ]; then
            missing="$missing $name"; continue
        fi
        { printf '%s\n' "$header"; cat "$a/outputs/$setName.out"; } > "$cache/base/$name.ko.txt"
    else
        translate "$a" "$dictsA" "$tmp/cacheA" "$header" "$cache/base/$name.ko.txt"
    fi
    translate "$b" "$dictsB" "$tmp/cacheB" "$header" "$cache/$name.ko.txt"
    chapters=$((chapters + 1))
    awk -v name="$name" -v budget=$((200 - printed)) -v counts="$tmp/counts" '
        FNR == NR {a[FNR] = $0; if (FNR > n) n = FNR; next}
        {b[FNR] = $0; if (FNR > n) n = FNR}
        END {
            for (i = 1; i <= n; ++i)
                if ((i in a) != (i in b) || a[i] != b[i]) line[++changed] = i
            shown = 0
            if (changed) printf "%s: %d changed line%s\n", name, changed, (changed == 1 ? "" : "s")
            for (k = 1; k <= changed && shown < 20 && shown < budget; ++k) {
                i = line[k]
                printf "  %d A: %s\n", i, (i in a) ? a[i] : "<missing>"
                printf "  %" length(i "") "s B: %s\n", "", (i in b) ? b[i] : "<missing>"
                ++shown
            }
            if (shown < changed) printf "  ... %d more\n", changed - shown
            print changed + 0, shown > counts
        }
    ' "$cache/base/$name.ko.txt" "$cache/$name.ko.txt"
    read -r count shown < "$tmp/counts"
    [ "$count" -eq 0 ] || changedSets=$((changedSets + 1))
    changedLines=$((changedLines + count)) printed=$((printed + shown))
done
[ -z "$missing" ] || echo "missing:$missing"
printf 'e2e: %d chapters compared, %d changed lines in %d chapters\n' \
    "$chapters" "$changedLines" "$changedSets"
[ "$chapters" -gt 0 ] || { echo 'no chapter compared; run tools/fetch-syosetu.sh' >&2; exit 2; }
[ "$changedLines" -eq 0 ]
