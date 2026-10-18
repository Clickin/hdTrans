#include "hdTrans/hdtrans.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "hdTrans/engine.h"

namespace {

std::mutex g_mutex;
std::uint64_t g_generation = 0;
std::unique_ptr<hdTrans::Engine> g_engine;
std::unique_ptr<hdTrans::Session> g_session;

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
    std::uint64_t generation;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        generation = g_generation;
    }
    auto e = std::make_unique<hdTrans::Engine>(false, hdTrans::CacheWrite::Off);
    if (!e->Open(SplitNul(dirs))) return -1;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (generation != g_generation) return -1;
    g_session.reset();
    g_engine = std::move(e);
    g_session = std::make_unique<hdTrans::Session>(*g_engine);
    ++g_generation;
    return 0;
}

void hdTrans_Terminate(void) {
    std::lock_guard<std::mutex> lock(g_mutex);
    ++g_generation;
    g_session.reset();
    g_engine.reset();
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
