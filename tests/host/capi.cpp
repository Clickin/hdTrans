#include "core/hdtrans.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    const std::string_view test = argv[1];
    std::string dirs = argv[2];
    dirs.append(2, '\0');
    if (hdTrans_Initialize(dirs.c_str()) != 0) return 2;

    char sentinel = 'x';
    char* out = &sentinel;
    size_t size = 123;
    int result;
    if (test == "null-text") {
        result = hdTrans_Translate(nullptr, 1, &out, &size);
    } else if (test == "embedded-nul") {
        result = hdTrans_Translate("A\0B", 3, &out, &size);
        const bool ok = result == 0 && out && out != &sentinel &&
                        std::string_view(out, size) == "A B" && out[size] == '\0';
        if (out != &sentinel) hdTrans_FreeMem(out);
        hdTrans_Terminate();
        return ok ? 0 : 1;
    } else if (test == "empty" || test == "null-empty") {
        result = hdTrans_Translate(test == "empty" ? "" : nullptr, 0, &out, &size);
        const bool ok = result == 0 && out && out != &sentinel && size == 0 && out[0] == '\0';
        if (out != &sentinel) hdTrans_FreeMem(out);
        hdTrans_Terminate();
        return ok ? 0 : 1;
    } else {
        char** output = test.ends_with("-out") ? nullptr : &out;
        size_t* length = test.ends_with("-size") ? nullptr : &size;
        if (test.starts_with("translate-")) result = hdTrans_Translate("", 0, output, length);
        else if (test.starts_with("user-")) result = hdTrans_UserDictError(output, length);
        else if (test.starts_with("filter-")) result = hdTrans_FilterErrors(output, length);
        else return 2;
    }
    const bool ok = result == -1 && out == &sentinel && size == 123;
    if (!ok) std::fprintf(stderr, "FAIL %s: expected -1 and unchanged output parameters\n", argv[1]);
    if (out != &sentinel) hdTrans_FreeMem(out);
    hdTrans_Terminate();
    return ok ? 0 : 1;
}
