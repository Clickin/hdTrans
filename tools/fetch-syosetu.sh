#!/bin/sh
# Downloads the 小説家になろう chapters that a test-data folder names but does not yet hold.
#   tools/fetch-syosetu.sh [DIR]      (default: $HDTRANS_TESTDATA/syosetu)
# Each DIR/<name>.ja.txt that holds only its first line, `# <title> <episode> ... <URL>`, gets the
# chapter after that line: its title, then one line per paragraph of the body (preface and
# afterword included), with ruby readings removed. Files with more lines are left alone. A failed
# chapter is reported and left as it was; the exit status is 0 unless the arguments are invalid.
set -eu
dir=${1:-${HDTRANS_TESTDATA:+$HDTRANS_TESTDATA/syosetu}}
[ $# -le 1 ] && [ -n "$dir" ] && [ -d "$dir" ] || { echo 'usage: tools/fetch-syosetu.sh [DIR]' >&2; exit 2; }
agent="hdtrans-e2e/1 (regression test of a translation engine${GITHUB_REPOSITORY:+; +https://github.com/$GITHUB_REPOSITORY})"
tmp=$(mktemp -d "${TMPDIR:-/tmp}/hdtrans-fetch.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
trap 'exit 2' HUP INT TERM

# HTML on stdin, chapter text on stdout; fails when the page has no chapter title or body.
extract() {
    LC_ALL=C awk '
        function utf8(c) {
            if (c < 128) return sprintf("%c", c)
            if (c < 2048) return sprintf("%c%c", 192 + int(c / 64), 128 + c % 64)
            if (c < 65536)
                return sprintf("%c%c%c", 224 + int(c / 4096), 128 + int(c / 64) % 64, 128 + c % 64)
            return sprintf("%c%c%c%c", 240 + int(c / 262144), 128 + int(c / 4096) % 64,
                128 + int(c / 64) % 64, 128 + c % 64)
        }
        function hex(s, i, v) {
            v = 0
            for (i = 1; i <= length(s); ++i) v = v * 16 + index("0123456789abcdef", tolower(substr(s, i, 1))) - 1
            return v
        }
        function unescape(s, out, ref, name) {
            out = ""
            while (match(s, /&(#[0-9]+|#[xX][0-9A-Fa-f]+|[A-Za-z]+);/)) {
                ref = substr(s, RSTART, RLENGTH); name = substr(ref, 2, RLENGTH - 2)
                out = out substr(s, 1, RSTART - 1)
                s = substr(s, RSTART + RLENGTH)
                if (name ~ /^#[xX]/) out = out utf8(hex(substr(name, 3)))
                else if (name ~ /^#/) out = out utf8(substr(name, 2) + 0)
                else if (name in named) out = out named[name]
                else out = out ref
            }
            return out s
        }
        function text(s) {
            if (s == "" || skip) return
            if (inTitle || inP) cur = cur unescape(s)
        }
        function trim(s) {
            for (;;) {
                if (sub(/^[ \t\n\r\f\v]+/, "", s) || sub(/[ \t\n\r\f\v]+$/, "", s)) continue
                if (substr(s, 1, 3) == ideoSpace) { s = substr(s, 4); continue }
                if (length(s) >= 3 && substr(s, length(s) - 2) == ideoSpace) { s = substr(s, 1, length(s) - 3); continue }
                return s
            }
        }
        function tag(t, end, name, cls) {
            end = t ~ /^\//
            if (end) t = substr(t, 2)
            if (!match(t, /^[A-Za-z0-9]+/)) return
            name = tolower(substr(t, 1, RLENGTH))
            cls = ""
            if (match(t, /[ \t\n]class="[^"]*"/)) cls = " " substr(t, RSTART + 8, RLENGTH - 9) " "
            gsub(/[\t\n]/, " ", cls)
            if (!end) {
                if (name == "h1" && index(cls, "p-novel__title")) { inTitle = 1; cur = "" }
                if (name == "div") {
                    if (textDiv) ++textDiv
                    else if (index(cls, " p-novel__text ")) textDiv = 1
                }
                if (textDiv && name == "p") { inP = 1; cur = "" }
                if (name == "rt" || name == "rp") ++skip
                if (t !~ /\/$/) return
            }
            if ((name == "rt" || name == "rp") && skip) --skip
            if (name == "h1" && inTitle) { title = trim(cur); hasTitle = 1; inTitle = 0 }
            if (name == "p" && inP) { gsub(/[\r\n]/, "", cur); paras[++n] = cur; inP = 0 }
            if (name == "div" && textDiv) --textDiv
        }
        BEGIN {
            ideoSpace = sprintf("%c%c%c", 227, 128, 128)
            named["amp"] = "&"; named["lt"] = "<"; named["gt"] = ">"; named["quot"] = "\""
            named["apos"] = "\047"; named["nbsp"] = sprintf("%c%c", 194, 160)
        }
        {
            line = pending $0 "\n"; pending = ""
            while ((lt = index(line, "<")) > 0) {
                rest = substr(line, lt + 1)
                gt = index(rest, ">")
                if (!gt) break
                text(substr(line, 1, lt - 1))
                tag(substr(rest, 1, gt - 1))
                line = substr(rest, gt + 1)
            }
            if (lt) pending = line
            else text(line)
        }
        END {
            if (!hasTitle || !n) exit 1
            print title
            for (i = 1; i <= n; ++i) print paras[i]
        }
    '
}

fetched=0 kept=0 missing=0 requests=0
for out in "$dir"/*.ja.txt; do
    [ -f "$out" ] || { echo "no .ja.txt in $dir" >&2; exit 2; }
    name=${out##*/}; name=${name%.ja.txt}
    if [ "$(wc -l < "$out")" -gt 1 ]; then
        kept=$((kept + 1)); continue
    fi
    header=$(head -n 1 "$out")
    url=${header##* }
    case $header in '# '*) ;; *) url= ;; esac
    if ! printf '%s\n' "$url" | grep -Eq '^https://ncode\.syosetu\.com/n[0-9a-z]+/([0-9]+/)?$'; then
        echo "warning: $name: no chapter URL at the end of the first line; missing" >&2
        missing=$((missing + 1)); continue
    fi
    [ "$requests" -eq 0 ] || sleep 1
    requests=$((requests + 1))
    if ! curl -fsS --max-time 60 -A "$agent" -H 'Accept-Language: ja' -o "$tmp/page.html" "$url"; then
        echo "warning: $name: download of $url failed; missing" >&2
        missing=$((missing + 1)); continue
    fi
    if ! extract < "$tmp/page.html" > "$tmp/body.txt"; then
        echo "warning: $name: no chapter text in $url; missing" >&2
        missing=$((missing + 1)); continue
    fi
    { printf '%s\n' "$header"; cat "$tmp/body.txt"; } > "$tmp/chapter.txt"
    mv "$tmp/chapter.txt" "$out"
    fetched=$((fetched + 1))
    echo "$name: $(($(wc -l < "$out") - 1)) lines from $url"
done
echo "fetch: $fetched downloaded, $kept already present, $missing missing"
