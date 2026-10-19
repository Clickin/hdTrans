#!/bin/sh
set -eu
cli=$1
tmp=$(mktemp -d "${TMPDIR:-/tmp}/hdtrans-config.XXXXXX")
tmp=$(cd "$tmp" && pwd -P)
trap 'rm -rf "$tmp"' EXIT
trap 'exit 1' HUP INT TERM
mkdir -p "$tmp/bin" "$tmp/settings/first" "$tmp/settings/second" "$tmp/other"
cp "$cli" "$tmp/bin/hdtrans"
cli=$tmp/bin/hdtrans
printf 'key\tko\tpos\tsem\tgram\tdict\trule\tflags\tprio\treading\tid\n猫\t고양이\tN110\t\t\tGN\t\t\t\t\t1\n' > "$tmp/settings/first/base.tsv"
printf 'key\tko\tpos\tsem\tgram\tdict\trule\tflags\tprio\treading\tid\n猫\t야옹이\tN110\t\t\tGN\t\t\t\t\t1\n' > "$tmp/settings/second/base.tsv"
printf 'dicts: first\nuserdict: ""\nehnd: ""\ncache: off\n' > "$tmp/settings/custom.yaml"
printf '猫' > "$tmp/input"
cd "$tmp/other"
"$cli" -config "$tmp/settings/custom.yaml" -in "$tmp/input" > "$tmp/out"
[ "$(cat "$tmp/out")" = 고양이 ]
"$cli" -dicts "$tmp/settings/second" -config "$tmp/settings/custom.yaml" -in "$tmp/input" > "$tmp/out"
[ "$(cat "$tmp/out")" = 야옹이 ]
printf 'dicts: ../settings/second\nuserdict: ""\nehnd: ""\ncache: off\n' > "$tmp/bin/hdtrans.config.yaml"
"$cli" -in "$tmp/input" > "$tmp/out"
[ "$(cat "$tmp/out")" = 야옹이 ]
mkdir -p "$tmp/bin/Ehnd"
printf '야옹이\t흰고양이\t1\t0\n' > "$tmp/bin/Ehnd/PostFilter_test.txt"
printf 'dicts: ../settings/second\nuserdict: ""\ncache: off\n' > "$tmp/bin/hdtrans.config.yaml"
"$cli" -in "$tmp/input" > "$tmp/out"
[ "$(cat "$tmp/out")" = 야옹이 ]
printf 'ehnd: true\n' >> "$tmp/bin/hdtrans.config.yaml"
"$cli" -in "$tmp/input" > "$tmp/out"
[ "$(cat "$tmp/out")" = 흰고양이 ]
printf 'ehnd: false\n' >> "$tmp/bin/hdtrans.config.yaml"
"$cli" -in "$tmp/input" > "$tmp/out"
[ "$(cat "$tmp/out")" = 야옹이 ]
"$cli" -ehnd "$tmp/bin/Ehnd" -in "$tmp/input" > "$tmp/out"
[ "$(cat "$tmp/out")" = 흰고양이 ]
printf 'dicts: second\nuserdict: ""\ncache: off\nehnd: true\n' > "$tmp/settings/custom.yaml"
"$cli" -config "$tmp/settings/custom.yaml" -in "$tmp/input" > "$tmp/out"
[ "$(cat "$tmp/out")" = 흰고양이 ]
rm "$tmp/bin/hdtrans.config.yaml"
"$cli" -dicts "$tmp/settings/second" -userdict "" -in "$tmp/input" > "$tmp/out"
[ "$(cat "$tmp/out")" = 야옹이 ]
printf 'dicts: [first, second]\nunknown: value\ncg: wrong\nuserdict: ""\nehnd: ""\ncache: off\n' > "$tmp/settings/custom.yaml"
"$cli" -config "$tmp/settings/custom.yaml" -in "$tmp/input" > "$tmp/out" 2> "$tmp/errors"
[ "$(cat "$tmp/out")" = 야옹이 ]
grep -F 'custom.yaml:2: unknown key: unknown' "$tmp/errors" > /dev/null
grep -F 'custom.yaml:3: cg: expected a boolean' "$tmp/errors" > /dev/null
printf 'dicts: first\nuserdict: ""\nehnd: ""\ncache: cache\n' > "$tmp/settings/custom.yaml"
HDTRANS_CACHE_DIR=off "$cli" -config "$tmp/settings/custom.yaml" -compile-dicts > "$tmp/cache-path"
case "$(cat "$tmp/cache-path")" in "$tmp/settings/cache/"*.hdsnap) ;; *) exit 1 ;; esac
status=0
"$cli" -config "$tmp/settings/custom.yaml" -no-cache -compile-dicts > /dev/null 2> "$tmp/errors" || status=$?
[ "$status" -eq 2 ]
"$cli" -config "$tmp/settings/custom.yaml" -no-cache -in "$tmp/input" > "$tmp/out"
[ "$(cat "$tmp/out")" = 고양이 ]
status=0
"$cli" -config "$tmp/missing.yaml" -in "$tmp/input" > /dev/null 2> "$tmp/errors" || status=$?
[ "$status" -eq 1 ]
echo 'CLI config: PASS'
