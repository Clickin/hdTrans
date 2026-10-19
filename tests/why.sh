#!/bin/sh
set -eu
export HDTRANS_CACHE_DIR=off
cli=$1
tmp=$(mktemp -d "${TMPDIR:-/tmp}/hdtrans-why.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
trap 'exit 1' HUP INT TERM
mkdir -p "$tmp/dicts" "$tmp/ehnd" "$tmp/home"
export HOME=$tmp/home
printf 'key\tko\tpos\tsem\tgram\tdict\trule\tid\n学生\t학생\tN110\t\t\tGN\t0\t1\n' > "$tmp/dicts/base.tsv"
printf '魔導具\t学生\t1\t0\n' > "$tmp/ehnd/PreFilter_test.txt"
printf '학생\t마도구\t1\t0\n' > "$tmp/ehnd/PostFilter_test.txt"
printf '魔導具' > "$tmp/input"
"$cli" -dicts "$tmp/dicts" -userdict '' -ehnd "$tmp/ehnd" -in "$tmp/input" > "$tmp/plain" 2> "$tmp/plain-err"
"$cli" -dicts "$tmp/dicts" -userdict '' -ehnd "$tmp/ehnd" -why -in "$tmp/input" > "$tmp/why" 2> "$tmp/logs"
cmp "$tmp/plain" "$tmp/why"
[ "$(cat "$tmp/why")" = 마도구 ]
[ ! -s "$tmp/plain-err" ]
grep -F '[REQUEST] 魔導具' "$tmp/logs" > /dev/null
grep -F 'PreFilter : [PreFilter_test.txt:0]' "$tmp/logs" > /dev/null
grep -F '[PRE] 学生' "$tmp/logs" > /dev/null
grep -F '[TRANS] 학생' "$tmp/logs" > /dev/null
grep -F 'PostFIlter : [PostFilter_test.txt:0]' "$tmp/logs" > /dev/null
grep -F '[POST] 마도구' "$tmp/logs" > /dev/null
"$cli" -dicts "$tmp/dicts" -userdict '' -ehnd "$tmp/ehnd" -why -stages "$tmp/stages" -in "$tmp/input" > "$tmp/staged" 2> "$tmp/staged-logs"
cmp "$tmp/plain" "$tmp/staged"
grep -F 'PreFilter : [PreFilter_test.txt:0]' "$tmp/staged-logs" > /dev/null
printf 'PASS CLI -why filter logs and unchanged stdout\n'
