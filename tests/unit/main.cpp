// Unit test runner. Usage: hdtrans_tests [name-substring-filter]
#include <cstdio>
#include <cstring>

#include "hdtest.h"

int main(int argc, char** argv) {
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int run = 0, failed = 0;
    for (const auto& t : hdtest::Registry()) {
        if (filter && !std::strstr(t.name, filter)) continue;
        hdtest::Cur() = {};
        t.fn();
        run++;
        if (hdtest::Cur().failures) {
            failed++;
            std::printf("FAIL %s\n", t.name);
        } else {
            std::printf("ok   %s\n", t.name);
        }
    }
    std::printf("%d test(s), %d failed\n", run, failed);
    return failed ? 1 : 0;
}
