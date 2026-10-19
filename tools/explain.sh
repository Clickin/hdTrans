#!/bin/sh
# Compare a single document line's stage records from two built CLIs.
set -eu
[ "$#" -eq 4 ] || { echo 'usage: tools/explain.sh <binA> <binB> <dicts> <input-line>' >&2; exit 2; }
[ -x "$1" ] && [ -x "$2" ] || { echo 'both binaries must be executable' >&2; exit 2; }
[ -d "$3" ] || { echo 'dictionary directory does not exist' >&2; exit 2; }
case $4 in *'
'*) echo 'expected one input line' >&2; exit 2 ;; esac
tmp=$(mktemp -d "${TMPDIR:-/tmp}/hdtrans-explain.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
trap 'exit 2' HUP INT TERM
export LC_ALL=C HDTRANS_CACHE_DIR=$tmp/cache
unset HDTRANS_SNAPSHOT HDTRANS_VERIFY_DICTS HDTRANS_DRIVER_DUMP
mkdir "$tmp/cache"
printf '%s' "$4" > "$tmp/input"
"$1" -dicts "$3" -stages "$tmp/A" -in "$tmp/input" > "$tmp/A.out"
"$2" -dicts "$3" -stages "$tmp/B" -in "$tmp/input" > "$tmp/B.out"
for tag in N SN P CH T CM L LC LD S K C R1 R2 R3 OUT; do
    a=$tmp/A/input.$tag.got.tsv
    b=$tmp/B/input.$tag.got.tsv
    [ -f "$a" ] && [ -f "$b" ] || { echo "missing stage dump: $tag" >&2; exit 2; }
    if ! cmp -s "$a" "$b"; then
        printf 'first differing stage: %s\n' "$tag"
        status=0
        diff -u "$a" "$b" || status=$?
        [ "$status" -le 1 ] || exit 2
        exit 1
    fi
done
cmp -s "$tmp/A.out" "$tmp/B.out" || { echo 'outputs differ despite identical stage records' >&2; exit 2; }
printf 'no stage differences (N SN P CH T CM L LC LD S K C R1 R2 R3 OUT)\noutput: '
cat "$tmp/A.out"
printf '\n'
