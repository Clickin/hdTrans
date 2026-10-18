#include "hdTrans/engine.h"

#include <cstddef>
#include <memory>

#include "hdTrans/dict/snapshot.h"
#include "hdTrans/engine/driver.h"
#include "hdTrans/engine/raw.h"
#include "hdTrans/engine/rules.h"

namespace hdTrans {

namespace {

// The lines of earlier calls a session keeps for its name registry; the oldest are dropped first.
constexpr std::size_t kMemoryLines = 2048;

// The dictionaries of dirs; null when the load fails or finds no dictionary file.
std::shared_ptr<const dict::Snapshot> LoadDictionaries(const std::vector<std::string>& dirs) {
    dict::LoadResult r = dict::LoadFolders(dirs);
    if (!r.snapshot || !r.snapshot->HasDictionary()) return nullptr;
    return std::move(r.snapshot);
}

}  // namespace

namespace internal {

struct SessionState {
    Memory memory;
};

std::shared_ptr<Generated> NewGenerated() { return std::make_shared<Generated>(); }

struct Access {
    static void Current(const Engine& e, std::shared_ptr<const dict::Snapshot>& snap,
                        std::shared_ptr<Generated>& generated) {
        std::lock_guard<std::mutex> lock(e.mu_);
        snap = e.snapshot_;
        generated = e.generated_;
    }
};

}  // namespace internal

bool Engine::Open(const std::vector<std::string>& dirs) {
    if (!internal::RulesLoaded()) return false;
    std::shared_ptr<const dict::Snapshot> s = LoadDictionaries(dirs);
    if (!s) return false;
    std::lock_guard<std::mutex> lock(mu_);
    dirs_ = dirs;
    snapshot_ = std::move(s);
    generated_ = internal::NewGenerated();
    return true;
}

bool Engine::Reload() {
    std::vector<std::string> dirs;
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (!snapshot_) return false;
        dirs = dirs_;
    }
    std::shared_ptr<const dict::Snapshot> s = LoadDictionaries(dirs);
    if (!s) return false;
    std::lock_guard<std::mutex> lock(mu_);
    snapshot_ = std::move(s);
    generated_ = internal::NewGenerated();
    return true;
}

bool Engine::loaded() const {
    std::lock_guard<std::mutex> lock(mu_);
    return snapshot_ != nullptr;
}

std::shared_ptr<const dict::Snapshot> Engine::Dictionaries() const {
    std::lock_guard<std::mutex> lock(mu_);
    return snapshot_;
}

Session::Session(const Engine& engine) : engine_(engine), state_(std::make_unique<internal::SessionState>()) {
    std::lock_guard<std::mutex> lock(engine.mu_);
    snapshot_ = engine.snapshot_;
    generated_ = engine.generated_;
}

Session::~Session() = default;

std::string Session::Translate(std::string_view text) {
    if (!snapshot_) return {};
    std::vector<std::string> lines;
    std::size_t start = 0;
    for (;;) {
        const std::size_t end = text.find('\n', start);
        lines.emplace_back(text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start));
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    const std::vector<std::string> translated =
        internal::TranslateDocument(*snapshot_, *generated_, lines, state_->memory, {});
    std::string out;
    for (std::size_t i = 0; i < translated.size(); i++) {
        if (i > 0) out += '\n';
        out += translated[i];
    }
    auto& memory = state_->memory.lines;
    if (memory.size() > kMemoryLines) memory.erase(memory.begin(), memory.begin() + static_cast<std::ptrdiff_t>(memory.size() - kMemoryLines));
    return out;
}

void Session::Reset() {
    std::lock_guard<std::mutex> lock(engine_.mu_);
    snapshot_ = engine_.snapshot_;
    generated_ = engine_.generated_;
    state_ = std::make_unique<internal::SessionState>();
}

namespace internal {

std::string TranslateRaw(const Engine& e, std::string_view line) {
    std::shared_ptr<const dict::Snapshot> snap;
    std::shared_ptr<Generated> generated;
    Access::Current(e, snap, generated);
    if (!snap) return {};
    return TranslateRawLine(*snap, *generated, line, {});
}

}  // namespace internal
}  // namespace hdTrans
