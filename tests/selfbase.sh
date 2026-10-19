#!/bin/sh
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
cli=$1
dicts=$here/tests/unit/dict/testdata/folder
tmp=$(mktemp -d "${TMPDIR:-/tmp}/hdtrans-selfbase.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
trap 'exit 1' HUP INT TERM
mkdir -p "$tmp/sets" "$tmp/data/lyrics" "$tmp/data/syosetu"
printf '学生\n猫\n' > "$tmp/sets/corpus.raw.in"
printf '学生\n猫\n' > "$tmp/sets/corpus2.each.in"
printf '学生\n猫\n' > "$tmp/sets/doc-sample.in"
printf '# song\n学生\tstudent\n# another\n猫\tcat\n' > "$tmp/data/lyrics/cases.tsv"
printf '学生\n猫\n' > "$tmp/data/syosetu/sample.ja.txt"
export HDTRANS_BIN=$cli HDTRANS_INPUTS=$tmp/sets HDTRANS_DATA=$tmp/data HDTRANS_RUNS=$tmp/runs
"$here/tools/snapshot.sh" "$dicts" first > "$tmp/snapshot.log"
"$here/tools/snapshot.sh" "$dicts" second >> "$tmp/snapshot.log"
"$here/tools/compare.sh" first second > "$tmp/compare.log"
grep -F '# total' "$tmp/compare.log" | grep -F '0 changed lines' > /dev/null
[ "$(awk 'END {print NR-1}' "$tmp/runs/first/sets.tsv")" -eq 6 ]
grep '^cpp_commit=' "$tmp/runs/first/MANIFEST" > /dev/null
grep '^dictionary_manifest_sha256=' "$tmp/runs/first/MANIFEST" > /dev/null
status=0
"$here/tools/snapshot.sh" "$dicts" first > "$tmp/duplicate.log" 2>&1 || status=$?
[ "$status" -eq 2 ]
printf '%s' "$(cat "$tmp/runs/first/outputs/corpus2.each.out")" > "$tmp/runs/second/outputs/corpus2.each.out"
status=0
"$here/tools/compare.sh" first second > "$tmp/newline.log" || status=$?
[ "$status" -eq 1 ]
grep -F '<no LF>' "$tmp/newline.log" > /dev/null
cp "$tmp/runs/first/outputs/corpus2.each.out" "$tmp/runs/second/outputs/corpus2.each.out"
printf 'changed\n' > "$tmp/runs/second/outputs/corpus.raw.out"
status=0
"$here/tools/compare.sh" first second > "$tmp/compare.log" || status=$?
[ "$status" -eq 1 ]
awk -F '\t' '$1 == "corpus.raw" && $2 == 1 && $5 == "changed" {found = 1} END {exit !found}' "$tmp/compare.log"
awk -F '\t' '$1 == "corpus.raw" && $2 == 2 && $5 == "<missing>" {found = 1} END {exit !found}' "$tmp/compare.log"
rm "$tmp/runs/second/outputs/corpus.raw.out"
status=0
"$here/tools/compare.sh" first second > "$tmp/missing.log" 2>&1 || status=$?
[ "$status" -eq 2 ]
"$here/tools/explain.sh" "$cli" "$cli" "$dicts" '学生' > "$tmp/explain.log"
grep '^no stage differences' "$tmp/explain.log" > /dev/null
export HDTRANS_CACHE_DIR=$tmp/cache
unset HDTRANS_DRIVER_DUMP HDTRANS_SNAPSHOT HDTRANS_VERIFY_DICTS
for mode in document raw each; do
    set --
    case $mode in raw) set -- -raw ;; each) set -- -each ;; esac
    "$cli" -dicts "$dicts" "$@" -in "$tmp/sets/corpus2.each.in" > "$tmp/plain.out"
    "$cli" -dicts "$dicts" "$@" -why -stages "$tmp/stages-$mode" \
        -in "$tmp/sets/corpus2.each.in" > "$tmp/dump.out" 2> "$tmp/why.log"
    cmp "$tmp/plain.out" "$tmp/dump.out"
    grep '^WHY-LINE' "$tmp/why.log" > /dev/null
    grep '^#LINE' "$tmp/stages-$mode/input.OUT.got.tsv" > /dev/null
done
HDTRANS_DRIVER_DUMP=$tmp/env-stages "$cli" -dicts "$dicts" \
    -in "$tmp/sets/corpus2.each.in" > "$tmp/env.out"
cmp "$tmp/stages-document/input.OUT.got.tsv" "$tmp/env-stages/input.OUT.got.tsv"
printf '%s' 'あ&#CSい。' > "$tmp/control.in"
"$cli" -dicts "$dicts" -raw -stages "$tmp/control" -in "$tmp/control.in" > /dev/null
grep '^CM' "$tmp/control/input.CM.got.tsv" > /dev/null
printf 'not a directory' > "$tmp/blocked"
status=0
"$cli" -dicts "$dicts" -stages "$tmp/blocked" -in "$tmp/sets/corpus2.each.in" > /dev/null 2> "$tmp/error.log" || status=$?
[ "$status" -eq 1 ]
cat > "$tmp/different" <<'SH'
#!/bin/sh
set -eu
next=0
dir=
for arg do
    if [ "$next" -eq 1 ]; then dir=$arg; next=0; fi
    [ "$arg" != -stages ] || next=1
done
"$HDTEST_CLI" "$@"
printf 'N\tchanged\n' >> "$dir/input.N.got.tsv"
SH
chmod +x "$tmp/different"
export HDTEST_CLI=$cli
status=0
"$here/tools/explain.sh" "$cli" "$tmp/different" "$dicts" '学生' > "$tmp/explain-diff.log" || status=$?
[ "$status" -eq 1 ]
grep '^first differing stage: N$' "$tmp/explain-diff.log" > /dev/null
grep '^+N' "$tmp/explain-diff.log" > /dev/null
echo 'self-baseline tools: PASS'
