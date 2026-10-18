// WebAssembly entry points: dictionary files arrive as buffers (hdTrans_Load, a null
// name finishes the set); translation is hdTrans_Translate from hdtrans.h.
#include <emscripten/emscripten.h>

#include <cstddef>

extern "C" {

EMSCRIPTEN_KEEPALIVE int hdTrans_Load(const char* /*name*/, const char* /*data*/, size_t /*n*/) { return -1; }

}  // extern "C"
