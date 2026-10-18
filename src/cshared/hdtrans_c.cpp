#include "hdTrans/hdtrans.h"

#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "hdTrans/engine.h"

namespace {

std::mutex g_mutex;
hdTrans::Engine* g_engine = nullptr;
hdTrans::Session* g_session = nullptr;

std::vector<std::string> SplitNul(const char* dirs) {
    std::vector<std::string> out;
    for (const char* p = dirs; p && *p; p += std::strlen(p) + 1) out.emplace_back(p);
    return out;
}

int Copy(const std::string& s, char** out, size_t* outn) {
    auto* buf = static_cast<char*>(std::malloc(s.size() + 1));
    if (!buf) return -1;
    std::memcpy(buf, s.data(), s.size());
    buf[s.size()] = 0;
    *out = buf;
    *outn = s.size();
    return 0;
}

}  // namespace

extern "C" {

int hdTrans_Initialize(const char* dirs) {
    std::lock_guard<std::mutex> lock(g_mutex);
    auto* e = new hdTrans::Engine;
    if (!e->Open(SplitNul(dirs))) {
        delete e;
        return -1;
    }
    delete g_session;
    delete g_engine;
    g_engine = e;
    g_session = new hdTrans::Session(*g_engine);
    return 0;
}

int hdTrans_Translate(const char* text, size_t n, char** out, size_t* outn) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_session) return -1;
    return Copy(g_session->Translate(std::string_view(text, n)), out, outn);
}

int hdTrans_Reload(void) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_engine) return -1;
    const bool ok = g_engine->Reload();
    g_session->Reset();
    return ok ? 0 : -1;
}

void hdTrans_FreeMem(void* p) { std::free(p); }

const char* hdTrans_Version(void) { return "0.0.0"; }

}  // extern "C"
