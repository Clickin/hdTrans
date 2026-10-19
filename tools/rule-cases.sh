#!/bin/sh
# Checks the rule cases in tests/rules/cases.tsv (or every table in a directory) against a built CLI.
#   tools/rule-cases.sh CLI DICTS [CASES] [--accept]
# CASES is a TSV with the header `rule input expected kind layer`. Every input is translated as its
# own session (-each -why). `expected` is either the whole Korean output, or
# `rule:L<n>=<target>`: some word of the line is chosen by local rule n with that target
# (the dictionary translation as -why prints it). kind is fix, keep or current. layer is the
# execution layer of the rule (docs/pipeline-layers.md).
# Failing rows are reported grouped by layer, each with its rule, input, expected and actual
# value, so that a change in one layer that breaks another layer's rows shows as such.
# --accept rewrites the expected value of every full-output row whose kind is current.
# Exit: 0 all rows pass, 1 a row fails, 2 invalid arguments or failed CLI.
set -eu
LC_ALL=C; export LC_ALL
[ $# -ge 2 ] || { echo "usage: tools/rule-cases.sh CLI DICTS [CASES] [--accept]" >&2; exit 2; }
cli=$1 dicts=$2; shift 2
here=$(cd "$(dirname "$0")/.." && pwd)
cases=$here/tests/rules/cases.tsv accept=
for a in "$@"; do
    if [ "$a" = --accept ]; then accept=1; else cases=$a; fi
done
[ -e "$cases" ] || { echo "no cases at $cases" >&2; exit 2; }
[ -d "$dicts" ] || { echo "no dictionaries at $dicts" >&2; exit 2; }
tmp=$(mktemp -d "${TMPDIR:-/tmp}/hdtrans-rules.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
trap 'exit 2' HUP INT TERM
# A directory holds one table per theme (tests/rules/cases/*.tsv); they are checked together.
if [ -d "$cases" ]; then
    [ -z "$accept" ] || { echo "--accept needs a single file" >&2; exit 2; }
    first=1
    for f in "$cases"/*.tsv; do
        head -n 1 "$f" | grep -q '^rule	input	expected	kind	layer$' || { echo "bad header in $f" >&2; exit 2; }
        if [ -n "$first" ]; then cat "$f"; first=; else tail -n +2 "$f"; fi
    done > "$tmp/all.tsv"
    cases=$tmp/all.tsv
fi
head -n 1 "$cases" | grep -q '^rule	input	expected	kind	layer$' || { echo "bad header in $cases" >&2; exit 2; }
tail -n +2 "$cases" | cut -f 2 > "$tmp/in.txt"
"$cli" -dicts "$dicts" -each -why -in "$tmp/in.txt" > "$tmp/out.txt" 2> "$tmp/why.txt" ||
    { echo "CLI failed" >&2; exit 2; }
awk -F '\t' -v OFS='\t' -v accept="$accept" -v fixed="$tmp/fixed.tsv" '
    FILENAME == ARGV[1] {
        if ($1 == "WHY" && $8 == "LocalRule") why[$2] = why[$2] "\n" "L" $9 "=" $10
        next
    }
    FILENAME == ARGV[2] { out[FNR] = $0; next }
    FNR == 1 { print > fixed; next }
    {
        n = FNR - 1
        if (NF != 5 || $1 == "" || $2 == "" || $3 == "" || $4 !~ /^(fix|keep|current)$/ || $5 == "") {
            printf "line %d: malformed row\n", FNR; bad++; print > fixed; next
        }
        if (!($5 in failed)) { failed[$5] = 0; order[++layers] = $5 }
        if ($3 ~ /^rule:L[0-9]+=/) {
            want = substr($3, 6)
            if (index(why[n] "\n", "\n" want "\n") == 0) {
                got = substr(why[n], 2); gsub(/\n/, "; ", got)
                report[$5] = report[$5] sprintf("%s: %s\n  expected %s\n  actual   rule:%s\n", $1, $2, $3, got)
                failed[$5]++; bad++
            }
        } else if (out[n] != $3) {
            if (accept != "" && $4 == "current") $3 = out[n]
            else {
                report[$5] = report[$5] sprintf("%s: %s\n  expected %s\n  actual   %s\n", $1, $2, $3, out[n])
                failed[$5]++; bad++
            }
        }
        print > fixed
        rows++
    }
    END {
        summary = ""
        for (i = 1; i <= layers; i++) {
            l = order[i]
            if (failed[l] > 0) printf "== layer %s: %d failed\n%s", l, failed[l], report[l]
            summary = summary sprintf("%s%s %d", i > 1 ? ", " : "", l, failed[l])
        }
        printf "rule cases: %d rows, %d failed (%s)\n", rows, bad, summary
        exit (bad > 0)
    }
' "$tmp/why.txt" "$tmp/out.txt" "$cases" || status=$?
[ -z "$accept" ] || cp "$tmp/fixed.tsv" "$cases"
exit "${status:-0}"
