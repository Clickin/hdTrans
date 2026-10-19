#!/bin/sh
# Capture the built CLI's default-mode output; never builds or reads expectations.
set -eu
[ "$#" -eq 2 ] || { echo 'usage: tools/snapshot.sh <dicts-dir> <name>' >&2; exit 2; }
here=$(cd "$(dirname "$0")/.." && pwd)
data=${HDTRANS_DATA:-$here/../hdtrans-data}
inputs=${HDTRANS_INPUTS:-$data/hdtrans-golden}
runs=${HDTRANS_RUNS:-$data/cpp-runs}
cli=${HDTRANS_BIN:-$here/build/mac/hdtrans}
name=$2
case $name in ''|.|..|*[!A-Za-z0-9._-]*) echo 'invalid run name' >&2; exit 2 ;; esac
[ -x "$cli" ] || { echo "no built CLI at $cli" >&2; exit 2; }
[ -d "$1" ] || { echo "no dictionaries at $1" >&2; exit 2; }
dicts=$(cd "$1" && pwd)
[ -f "$inputs/corpus.raw.in" ] && [ -f "$inputs/corpus2.each.in" ] || {
    echo "missing corpus inputs in $inputs" >&2; exit 2;
}
[ -f "$data/lyrics/cases.tsv" ] || { echo 'missing lyrics/cases.tsv' >&2; exit 2; }
mkdir -p "$runs"
mkdir "$runs/$name" || { echo 'run already exists or cannot be created' >&2; exit 2; }
run=$(cd "$runs/$name" && pwd)
mkdir "$run/inputs" "$run/outputs" "$run/timing" "$run/.cache"
trap 'rm -rf "$run/.cache"' EXIT
trap 'exit 1' HUP INT TERM
export LC_ALL=C HDTRANS_CACHE_DIR=$run/.cache
unset HDTRANS_SNAPSHOT HDTRANS_VERIFY_DICTS HDTRANS_DRIVER_DUMP
started=$(date +%s)
sha() { shasum -a 256 "$1" | awk '{print $1}'; }
dictionary_hashes() {
    (cd "$dicts"
     for file in *.tsv; do
         [ -f "$file" ] || { echo 'no dictionary TSV files' >&2; exit 2; }
         shasum -a 256 "$file"
     done)
}
dictionary_hashes > "$run/DICTS_SHA256"
for input in "$inputs"/*.in; do
    [ -f "$input" ] || continue
    setName=${input##*/}; setName=${setName%.in}
    case $setName in ''|*[!A-Za-z0-9._-]*) echo "invalid set name: $setName" >&2; exit 2 ;; esac
    cp "$input" "$run/inputs/$setName.in"
done
found=0
for input in "$run/inputs"/doc-*.in; do [ ! -f "$input" ] || found=1; done
[ "$found" -eq 1 ] || { echo 'no document input sets' >&2; exit 2; }
awk -v out="$run/inputs" '
    /^#/ { if (active) close(path); active = 0; next }
    /^[[:space:]]*$/ { next }
    {
        tab = index($0, "\t")
        if (!tab) { print "invalid lyrics row " NR > "/dev/stderr"; exit 2 }
        if (!active) { path = sprintf("%s/lyrics-%02d.in", out, ++song); active = 1 }
        print substr($0, 1, tab - 1) > path
    }
    END { if (!song) exit 2 }
' "$data/lyrics/cases.tsv"
found=0
for input in "$data/syosetu"/*.ja.txt; do
    [ -f "$input" ] || continue
    setName=${input##*/}; setName=syosetu-${setName%.ja.txt}
    case $setName in *[!A-Za-z0-9._-]*) echo "invalid set name: $setName" >&2; exit 2 ;; esac
    cp "$input" "$run/inputs/$setName.in"
    found=1
done
[ "$found" -eq 1 ] || { echo 'no syosetu documents' >&2; exit 2; }
printf 'set\tmode\tlines\tseconds\tinput_sha256\toutput_sha256\n' > "$run/sets.tsv"
for input in "$run/inputs"/*.in; do
    setName=${input##*/}; setName=${setName%.in}
    mode=document
    set --
    case $setName in *.raw) mode=raw; set -- -raw ;; *.each) mode=each; set -- -each ;; esac
    /usr/bin/time -p "$cli" -dicts "$dicts" "$@" -in "$input" \
        > "$run/outputs/$setName.out" 2> "$run/timing/$setName.log"
    seconds=$(awk '$1 == "real" {value = $2} END {print value}' "$run/timing/$setName.log")
    [ -n "$seconds" ] || { echo "missing timing for $setName" >&2; exit 1; }
    lines=$(awk 'END {print NR+0}' "$input")
    printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$setName" "$mode" "$lines" "$seconds" \
        "$(sha "$input")" "$(sha "$run/outputs/$setName.out")" >> "$run/sets.tsv"
    printf '%s: %s s (%s lines, %s)\n' "$setName" "$seconds" "$lines" "$mode"
done
dictionary_hashes > "$run/DICTS_SHA256.after"
cmp -s "$run/DICTS_SHA256" "$run/DICTS_SHA256.after" || {
    echo 'dictionaries changed during the snapshot; run is incomplete' >&2; exit 1;
}
rm "$run/DICTS_SHA256.after"
finished=$(date +%s)
dirty=0
[ -z "$(git -C "$here" status --porcelain --untracked-files=normal)" ] || dirty=1
{
    echo 'format=1'
    echo "cpp_commit=$(git -C "$here" rev-parse HEAD)"
    echo "cpp_dirty=$dirty"
    echo "binary_sha256=$(sha "$cli")"
    echo "dictionary_manifest_sha256=$(sha "$run/DICTS_SHA256")"
    echo "sets_manifest_sha256=$(sha "$run/sets.tsv")"
    echo 'mode=default'
    echo 'cache=run-local, cold first set, reused thereafter'
    echo "created_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "wall_seconds=$((finished - started))"
    echo "translation_seconds=$(awk -F '\t' 'NR > 1 {sum += $4} END {printf "%.2f", sum}' "$run/sets.tsv")"
    echo "sets=$(awk 'END {print NR-1}' "$run/sets.tsv")"
} > "$run/MANIFEST"
printf 'snapshot: %s (%s s wall)\n' "$run" "$((finished - started))"
