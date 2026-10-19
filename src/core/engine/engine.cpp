#include "core/engine.h"

#include <cstddef>
#include <memory>

#include "core/dict/snapshot.h"
#include "core/engine/driver.h"
#include "core/engine/raw.h"
#include "core/engine/rules.h"
#include "core/filter/ehnd.h"
#include "core/filter/filter.h"
#include "core/output/families.h"
#include "core/userdict/conjugating.h"
#include "core/text/lines.h"

namespace hdTrans {

namespace {

// The dictionaries of dirs; null when the load fails or finds no dictionary file.
std::shared_ptr<const dict::Snapshot> LoadDictionaries(const std::vector<std::string>& dirs, bool verify, CacheWrite cacheWrite) {
    dict::LoadResult r = dict::LoadFolders(dirs, {}, verify, cacheWrite == CacheWrite::Sync);
    if (!r.snapshot || !r.snapshot->HasDictionary()) return nullptr;
    return std::move(r.snapshot);
}

const std::vector<dict::File> kNoFiles;

// The filters of the prefilter and postfilter files of snap and of the Ehnd folder dir
// (none when empty), and the lines they skip in messages; read is false when dir cannot
// be read.
std::shared_ptr<const filter::Filters> LoadFilters(const dict::Snapshot* snap, const std::string& dir, bool& read,
                                                   std::vector<std::string>& messages) {
    std::vector<filter::Source> sources;
    std::vector<filter::Error> errors;
    for (const dict::File& f : snap ? snap->PlainFiles() : kNoFiles) {
        if (f.layer != dict::FileLayer::PreFilter && f.layer != dict::FileLayer::PostFilter) continue;
        const auto kind = f.layer == dict::FileLayer::PreFilter ? filter::Source::Kind::PreFilter
                                                                 : filter::Source::Kind::PostFilter;
        sources.push_back({kind, f.name, std::string(dict::internal::WithoutHeader(f.text)), true});
    }
    read = dir.empty() || filter::ReadEhndFolder(dir, sources, errors);
    std::shared_ptr<const filter::Filters> out = filter::Build(sources, errors);
    messages.clear();
    for (const filter::Error& e : errors) messages.push_back(e.file + ":" + std::to_string(e.line) + ": " + e.message);
    return out;
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
                        std::shared_ptr<Generated>& generated, std::shared_ptr<const filter::Filters>& filters) {
        std::lock_guard<std::mutex> lock(e.mu_);
        snap = e.snapshot_;
        generated = e.generated_;
        filters = e.filters_;
    }
};

}  // namespace internal

bool Engine::Open(const std::vector<std::string>& dirs, bool verifyDicts) {
    if (!internal::RulesLoaded()) return false;
    std::shared_ptr<const dict::Snapshot> s = LoadDictionaries(dirs, verifyDicts, cacheWrite_);
    if (!s) return false;
    std::string ehnd;
    {
        std::lock_guard<std::mutex> lock(mu_);
        ehnd = ehndDir_;
    }
    bool read;
    std::vector<std::string> messages;
    std::shared_ptr<const filter::Filters> filters = LoadFilters(s.get(), ehnd, read, messages);
    std::lock_guard<std::mutex> lock(mu_);
    dirs_ = dirs;
    verifyDicts_ = verifyDicts;
    snapshot_ = std::move(s);
    generated_ = internal::NewGenerated(*snapshot_);
    filters_ = std::move(filters);
    filterErrors_ = std::move(messages);
    return true;
}

bool Engine::Reload() {
    std::vector<std::string> dirs;
    bool verify;
    std::string ehnd;
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (!snapshot_) return false;
        dirs = dirs_;
        verify = verifyDicts_;
        ehnd = ehndDir_;
    }
    std::shared_ptr<const dict::Snapshot> s = LoadDictionaries(dirs, verify, cacheWrite_);
    if (!s) return false;
    bool read;
    std::vector<std::string> messages;
    std::shared_ptr<const filter::Filters> filters = LoadFilters(s.get(), ehnd, read, messages);
    std::lock_guard<std::mutex> lock(mu_);
    snapshot_ = std::move(s);
    generated_ = internal::NewGenerated(*snapshot_);
    filters_ = std::move(filters);
    filterErrors_ = std::move(messages);
    return true;
}

bool Engine::OpenEhnd(std::string_view dir) {
    const std::shared_ptr<const dict::Snapshot> s = Dictionaries();
    bool read;
    std::vector<std::string> messages;
    std::shared_ptr<const filter::Filters> filters = LoadFilters(s.get(), std::string(dir), read, messages);
    if (!read) return false;
    std::lock_guard<std::mutex> lock(mu_);
    ehndDir_ = dir;
    filters_ = std::move(filters);
    filterErrors_ = std::move(messages);
    return true;
}

std::vector<std::string> Engine::FilterErrors() const {
    std::lock_guard<std::mutex> lock(mu_);
    return filterErrors_;
}

std::shared_ptr<const filter::Filters> Engine::Filters() const {
    std::lock_guard<std::mutex> lock(mu_);
    return filters_;
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
    filters_ = engine.filters_;
}

Session::~Session() = default;

std::string Session::Translate(std::string_view text) {
    if (!snapshot_) return {};
    std::vector<std::string> lines;
    for (const std::string_view line : text::Lines(text)) lines.emplace_back(line);
    internal::TranslationOptions options;
    options.reference = reference_;
    options.filters = filters_.get();
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
    state_ = std::make_unique<internal::SessionState>();
    snapshot_ = engine_.snapshot_;
    generated_ = engine_.generated_;
    filters_ = engine_.filters_;
}

namespace internal {

std::string TranslateRaw(const Engine& e, std::string_view line) {
    std::shared_ptr<const dict::Snapshot> snap;
    std::shared_ptr<Generated> generated;
    std::shared_ptr<const filter::Filters> filters;
    Access::Current(e, snap, generated, filters);
    if (!snap) return {};
    TranslationOptions options;
    options.reference = Access::Reference(e);
    options.filters = filters.get();
    return TranslateRawLine(*snap, *generated, line, {}, options);
}

}  // namespace internal
}  // namespace hdTrans
