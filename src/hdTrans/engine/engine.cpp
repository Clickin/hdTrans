#include "hdTrans/engine.h"

#include <cstddef>
#include <memory>

#include "hdTrans/dict/snapshot.h"
#include "hdTrans/engine/driver.h"
#include "hdTrans/engine/raw.h"
#include "hdTrans/engine/rules.h"
#include "hdTrans/output/families.h"
#include "hdTrans/userdict/conjugating.h"
#include "hdTrans/text/lines.h"

namespace hdTrans {

namespace {

// The dictionaries of dirs; null when the load fails or finds no dictionary file.
std::shared_ptr<const dict::Snapshot> LoadDictionaries(const std::vector<std::string>& dirs, bool verify, CacheWrite cacheWrite) {
    dict::LoadResult r = dict::LoadFolders(dirs, {}, verify, cacheWrite == CacheWrite::Sync);
    if (!r.snapshot || !r.snapshot->HasDictionary()) return nullptr;
    return std::move(r.snapshot);
}

}  // namespace

namespace internal {

struct SessionState {
    Memory memory;
};

// Copies the marker families the dictionaries made at load into out, under the ids they have
// there; the dictionaries' targets refer to these ids.
void CopyGenerated(const userdict::Families& from, output::Families& out) {
    for (const auto& [id, rows] : from.GeneratedFamilies()) out.Insert(id[0], id, rows);
}

std::shared_ptr<Generated> NewGenerated(const dict::Snapshot& snap) {
    auto generated = std::make_shared<Generated>();
    CopyGenerated(snap.GeneratedFamilies(), generated->families);
    return generated;
}

struct Access {
    static bool Reference(const Engine& e) { return e.reference_; }

    static void Current(const Engine& e, std::shared_ptr<const dict::Snapshot>& snap,
                        std::shared_ptr<Generated>& generated) {
        std::lock_guard<std::mutex> lock(e.mu_);
        snap = e.snapshot_;
        generated = e.generated_;
    }
};

}  // namespace internal

bool Engine::Open(const std::vector<std::string>& dirs, bool verifyDicts) {
    if (!internal::RulesLoaded()) return false;
    std::shared_ptr<const dict::Snapshot> s = LoadDictionaries(dirs, verifyDicts, cacheWrite_);
    if (!s) return false;
    std::lock_guard<std::mutex> lock(mu_);
    dirs_ = dirs;
    verifyDicts_ = verifyDicts;
    snapshot_ = std::move(s);
    generated_ = internal::NewGenerated(*snapshot_);
    return true;
}

bool Engine::Reload() {
    std::vector<std::string> dirs;
    bool verify;
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (!snapshot_) return false;
        dirs = dirs_;
        verify = verifyDicts_;
    }
    std::shared_ptr<const dict::Snapshot> s = LoadDictionaries(dirs, verify, cacheWrite_);
    if (!s) return false;
    std::lock_guard<std::mutex> lock(mu_);
    snapshot_ = std::move(s);
    generated_ = internal::NewGenerated(*snapshot_);
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

Session::Session(const Engine& engine, bool reference)
    : engine_(engine), reference_(engine.reference_ || reference),
      state_(std::make_unique<internal::SessionState>()) {
    std::lock_guard<std::mutex> lock(engine.mu_);
    snapshot_ = engine.snapshot_;
    generated_ = engine.generated_;
}

Session::~Session() = default;

std::string Session::Translate(std::string_view text) {
    if (!snapshot_) return {};
    std::vector<std::string> lines;
    for (const std::string_view line : text::Lines(text)) lines.emplace_back(line);
    internal::TranslationOptions options;
    options.reference = reference_;
    const std::vector<std::string> translated =
        internal::TranslateDocument(*snapshot_, *generated_, lines, state_->memory, {}, options);
    std::string out;
    for (std::size_t i = 0; i < translated.size(); i++) {
        if (i > 0) out += '\n';
        out += translated[i];
    }
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
    TranslationOptions options;
    options.reference = Access::Reference(e);
    return TranslateRawLine(*snap, *generated, line, {}, options);
}

}  // namespace internal
}  // namespace hdTrans
