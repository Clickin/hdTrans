#include "hdTrans/engine.h"

#include "hdTrans/dict/snapshot.h"
#include "hdTrans/engine/raw.h"

namespace hdTrans {

namespace {

// The dictionaries of dirs; null when the load fails or finds no dictionary file.
std::shared_ptr<const dict::Snapshot> LoadDictionaries(const std::vector<std::string>& dirs) {
    dict::LoadResult r = dict::LoadFolders(dirs);
    if (!r.snapshot || !r.snapshot->HasDictionary()) return nullptr;
    return std::move(r.snapshot);
}

}  // namespace

bool Engine::Open(const std::vector<std::string>& dirs) {
    std::shared_ptr<const dict::Snapshot> s = LoadDictionaries(dirs);
    if (!s) return false;
    std::lock_guard<std::mutex> lock(mu_);
    dirs_ = dirs;
    snapshot_ = std::move(s);
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

Session::Session(const Engine& engine) : engine_(engine), snapshot_(engine.Dictionaries()) {}

std::string Session::Translate(std::string_view text) {
    if (!snapshot_) return {};
    return std::string(text);
}

void Session::Reset() { snapshot_ = engine_.Dictionaries(); }

namespace internal {

std::string TranslateRaw(const Engine& e, std::string_view line) {
    if (!e.loaded()) return {};
    return std::string(line);
}

}  // namespace internal
}  // namespace hdTrans
