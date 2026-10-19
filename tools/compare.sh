#!/bin/sh
# TSV detail rows escape backslash, TAB and CR. Exit: 0 identical, 1 changed, 2 invalid run.
set -eu
[ "$#" -eq 2 ] || { echo 'usage: tools/compare.sh <runA> <runB>' >&2; exit 2; }
here=$(cd "$(dirname "$0")/.." && pwd)
data=${HDTRANS_DATA:-$here/../hdtrans-data}
runs=${HDTRANS_RUNS:-$data/cpp-runs}
resolve() {
    if [ -d "$1" ]; then (cd "$1" && pwd); else (cd "$runs/$1" && pwd); fi
}
a=$(resolve "$1") || exit 2
b=$(resolve "$2") || exit 2
for run in "$a" "$b"; do
    [ -f "$run/MANIFEST" ] && [ -f "$run/sets.tsv" ] || { echo "incomplete run: $run" >&2; exit 2; }
done
tmp=$(mktemp -d "${TMPDIR:-/tmp}/hdtrans-compare.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
trap 'exit 2' HUP INT TERM
export LC_ALL=C
awk -F '\t' 'FNR > 1 {print $1}' "$a/sets.tsv" "$b/sets.tsv" | sort -u > "$tmp/sets"
printf 'set\tline\tinput\tA\tB\n'
total=0
sets=0
while IFS= read -r name; do
    case $name in ''|*[!A-Za-z0-9._-]*) echo 'invalid set name in manifest' >&2; exit 2 ;; esac
    ai=/dev/null; bi=/dev/null; ao=/dev/null; bo=/dev/null
    if awk -F '\t' -v name="$name" '$1 == name {found = 1} END {exit !found}' "$a/sets.tsv"; then
        ai=$a/inputs/$name.in; ao=$a/outputs/$name.out
        [ -f "$ai" ] && [ -f "$ao" ] || { echo "missing files in A: $name" >&2; exit 2; }
    fi
    if awk -F '\t' -v name="$name" '$1 == name {found = 1} END {exit !found}' "$b/sets.tsv"; then
        bi=$b/inputs/$name.in; bo=$b/outputs/$name.out
        [ -f "$bi" ] && [ -f "$bo" ] || { echo "missing files in B: $name" >&2; exit 2; }
    fi
    awk -v name="$name" -v iaBytes="$(wc -c < "$ai")" -v ibBytes="$(wc -c < "$bi")" \
        -v aBytes="$(wc -c < "$ao")" -v bBytes="$(wc -c < "$bo")" '
        function esc(text, out, i, ch) {
            out = ""
            for (i = 1; i <= length(text); ++i) {
                ch = substr(text, i, 1)
                if (ch == "\\") out = out "\\\\"
                else if (ch == "\t") out = out "\\t"
                else if (ch == "\r") out = out "\\r"
                else out = out ch
            }
            return out
        }
        phase == 1 {ia[FNR] = $0; iaN = FNR; iaRead += length($0) + 1; if (FNR > n) n = FNR}
        phase == 2 {ib[FNR] = $0; ibN = FNR; ibRead += length($0) + 1; if (FNR > n) n = FNR}
        phase == 3 {a[FNR] = $0; aN = FNR; aRead += length($0) + 1; if (FNR > n) n = FNR}
        phase == 4 {b[FNR] = $0; bN = FNR; bRead += length($0) + 1; if (FNR > n) n = FNR}
        END {
            changed = 0
            for (i = 1; i <= n; ++i) {
                hasA = i in a; hasB = i in b; hasIA = i in ia; hasIB = i in ib
                iaNoLF = i == iaN && iaBytes < iaRead
                ibNoLF = i == ibN && ibBytes < ibRead
                aNoLF = i == aN && aBytes < aRead
                bNoLF = i == bN && bBytes < bRead
                if (hasA == hasB && ("x" a[i]) == ("x" b[i]) && aNoLF == bNoLF &&
                    hasIA == hasIB && ("x" ia[i]) == ("x" ib[i]) && iaNoLF == ibNoLF) continue
                inputA = esc(ia[i]) (iaNoLF ? " <no LF>" : "")
                inputB = esc(ib[i]) (ibNoLF ? " <no LF>" : "")
                input = hasIA ? inputA : inputB
                if (hasIA && hasIB && inputA != inputB) input = inputA " -> " inputB
                printf "%s\t%d\t%s\t%s\t%s\n", name, i, input,
                    (hasA ? esc(a[i]) (aNoLF ? " <no LF>" : "") : "<missing>"),
                    (hasB ? esc(b[i]) (bNoLF ? " <no LF>" : "") : "<missing>")
                ++changed
            }
            printf "# count\t%s\t%d\n", name, changed
        }
    ' phase=1 "$ai" phase=2 "$bi" phase=3 "$ao" phase=4 "$bo" > "$tmp/detail"
    cat "$tmp/detail"
    changed=$(tail -n 1 "$tmp/detail" | cut -f 3)
    total=$((total + changed))
    sets=$((sets + 1))
done < "$tmp/sets"
[ "$sets" -gt 0 ] || { echo 'no sets in run manifests' >&2; exit 2; }
printf '# total\t%d changed lines\t%d sets\n' "$total" "$sets"
[ "$total" -eq 0 ]
