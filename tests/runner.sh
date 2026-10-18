#!/bin/sh
# Runner contract checks use an isolated registry, not the engine fixtures.
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
tmp=$(mktemp -d "${TMPDIR:-/tmp}/hdtrans-runner.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
cat > "$tmp/probe.cpp" <<'CPP'
#include "hdtest.h"
#include <filesystem>
#include <fstream>
HD_TEST(pass) { HD_CHECK(true); }
HD_TEST(skip) { HD_SKIP("fixture absent"); }
HD_TEST(fail) { HD_CHECK(false); }
HD_TEST(mixed) { HD_CHECK(false); HD_SKIP("fixture absent"); }
HD_TEST(cache_write) {
    std::ofstream(std::filesystem::path(std::getenv("HDTEST_REAL_CACHE")) / "changed") << "changed";
}
CPP
"${CXX:-c++}" -std=c++20 -Wall -Wextra -Werror -fno-exceptions -fno-rtti \
    -I "$here/tests/unit" "$here/tests/unit/main.cpp" "$tmp/probe.cpp" -o "$tmp/runner"
check() {
    want=$1; summary=$2; filter=$3; require=$4
    status=0
    HDTRANS_REQUIRE_ALL=$require "$tmp/runner" "$filter" > "$tmp/log" 2>&1 || status=$?
    [ "$status" = "$want" ] || { cat "$tmp/log"; echo "unexpected exit: $status" >&2; exit 1; }
    grep -Fx "$summary" "$tmp/log" > /dev/null || { cat "$tmp/log"; exit 1; }
}
check 0 '1 run, 0 failed, 0 skipped' pass 1
check 0 '1 run, 0 failed, 1 skipped' skip 0
! grep '^ok .*skip' "$tmp/log"
grep '^SKIP skip: fixture absent$' "$tmp/log" > /dev/null
check 1 '1 run, 0 failed, 1 skipped' skip 1
check 1 '1 run, 1 failed, 0 skipped' fail 0
check 1 '1 run, 1 failed, 1 skipped' mixed 0
check 1 '0 run, 0 failed, 0 skipped' no_such_test 0
mkdir -p "$tmp/home/Library/Caches/hdtrans" "$tmp/xdg/hdtrans"
case $(uname -s) in
    Darwin) real=$tmp/home/Library/Caches/hdtrans ;;
    *) real=$tmp/xdg/hdtrans ;;
esac
export HOME=$tmp/home XDG_CACHE_HOME=$tmp/xdg HDTEST_REAL_CACHE=$real
check 1 '1 run, 1 failed, 0 skipped' cache_write 1
grep 'per-user cache changed' "$tmp/log" > /dev/null
echo 'runner semantics: PASS'
