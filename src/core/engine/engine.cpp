#include "core/engine.h"

#include <chrono>
#include <cstddef>
#include <memory>
#include <string>

#include "core/dict/snapshot.h"
#include "core/engine/driver.h"
#include "core/engine/raw.h"
#include "core/engine/rules.h"
#include "core/engine/user_watch.h"
#include "core/filter/ehnd.h"
#include "core/filter/filter.h"
#include "core/output/families.h"
#include "core/userdict/conjugating.h"
#include "core/text/lines.h"

namespace hdTrans {

namespace {

constexpr std::chrono::seconds kCheckInterval{1};

bool Loaded(const dict::LoadResult& r) { return r.snapshot && r.snapshot->HasDictionary(); }

const std::vector<dict::File> kNoFiles;

// The filters of the prefilter and postfilter files of a snapshot and of an Ehnd folder.
struct FilterLoad {
    std::shared_ptr<const filter::Filters> filters;
    std::vector<std::string> errors;  // the lines skipped, as "file:line: message"
    internal::FolderVersion version;  // the Ehnd folder as read
    bool read = true;                 // false when the Ehnd folder cannot be read
};

// The filters of snap and of the Ehnd folder dir (none when empty).
FilterLoad LoadFilters(const dict::Snapshot* snap, const std::string& dir) {
    FilterLoad out;
    out.version = internal::FolderVersionOf(dir);
    std::vector<filter::Source> sources;
    std::vector<filter::Error> errors;
    for (const dict::File& f : snap ? snap->PlainFiles() : kNoFiles) {
        if (f.layer != dict::FileLayer::PreFilter && f.layer != dict::FileLayer::PostFilter) continue;
        const auto kind = f.layer == dict::FileLayer::PreFilter ? filter::Source::Kind::PreFilter
                                                                 : filter::Source::Kind::PostFilter;
        sources.push_back({kind, f.name, std::string(dict::internal::WithoutHeader(f.text)), true});
    }
    out.read = dir.empty() || filter::ReadEhndFolder(dir, sources, errors);
    out.filters = filter::Build(sources, errors);
    for (const filter::Error& e : errors) out.errors.push_back(e.file + ":" + std::to_string(e.line) + ": " + e.message);
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

    // Makes r, the load number ticket that read version of the user dictionary file,
    // current unless a later load already is. With bare, r was loaded without the user
    // dictionary file after userErrors kept it from loading; a failed r leaves the
    // dictionaries as they are. e.mu_ is held.
    static bool Finish(Engine& e, std::uint64_t ticket, const FileVersion& version, dict::LoadResult r, bool bare,
                       std::vector<dict::LoadError> userErrors, FilterLoad filters) {
        if (ticket < e.current_) return Loaded(r);
        UserWatch& w = *e.watch_;
        if (!Loaded(r)) {
            w.ehnd.failed = std::move(filters.version);
            w.ehnd.hasFailed = true;
        }
        if (!Loaded(r) || bare) {
            w.watch.failed = version;
            w.watch.hasFailed = true;
            w.errors.clear();
            for (dict::LoadError& error : bare ? userErrors : r.errors) {
                if (error.kind != dict::LoadErrorKind::UnknownPos) w.errors.push_back(std::move(error));
            }
            if (!Loaded(r)) return false;
            w.watch.loaded = {};
        } else {
            w.watch.loaded = version;
            w.watch.hasFailed = false;
            w.errors.clear();
        }
        e.current_ = ticket;
        e.snapshot_ = std::move(r.snapshot);
        e.generated_ = NewGenerated(*e.snapshot_);
        e.filters_ = std::move(filters.filters);
        e.filterErrors_ = std::move(filters.errors);
        w.ehnd.loaded = std::move(filters.version);
        w.ehnd.hasFailed = false;
        return true;
    }

    // The filters of r, the outcome of a load with the Ehnd folder dir.
    static FilterLoad FiltersOf(const dict::LoadResult& r, const std::string& dir) {
        if (!Loaded(r)) {
            FilterLoad failed;
            failed.version = FolderVersionOf(dir);
            return failed;
        }
        return LoadFilters(r.snapshot.get(), dir);
    }

    // Loads dirs and userFile and makes them current unless a load started later already
    // did. With open, a user dictionary file that keeps the folders from loading is left
    // out, and on success dirs and userFile become the engine's.
    static bool Load(Engine& e, const std::vector<std::string>& dirs, bool verify, const std::string& userFile,
                     bool open) {
        std::uint64_t ticket;
        std::string ehnd;
        {
            std::lock_guard<std::mutex> lock(e.mu_);
            ticket = ++e.loads_;
            ehnd = e.ehndDir_;
        }
        const bool cacheWrite = e.cacheWrite_ == CacheWrite::Sync;
        const FileVersion version = VersionOf(userFile);
        dict::LoadResult r = dict::LoadFolders(dirs, {}, verify, cacheWrite, userFile);
        std::vector<dict::LoadError> userErrors;
        bool bare = false;
        if (!Loaded(r) && open && !userFile.empty()) {
            dict::LoadResult folders = dict::LoadFolders(dirs, {}, verify, cacheWrite);
            if (Loaded(folders)) {
                userErrors = std::move(r.errors);
                r = std::move(folders);
                bare = true;
            }
        }
        FilterLoad filters = FiltersOf(r, ehnd);
        std::lock_guard<std::mutex> lock(e.mu_);
        if (!Finish(e, ticket, version, std::move(r), bare, std::move(userErrors), std::move(filters))) return false;
        if (open) {
            e.dirs_ = dirs;
            e.verifyDicts_ = verify;
            e.userFile_ = userFile;
        }
        return true;
    }

    // Starts a background reload when the user dictionary file or the Ehnd folder is due
    // at now; e.mu_ is held.
    static bool StartReload(const Engine& e, std::chrono::steady_clock::time_point now,
                            std::chrono::steady_clock::duration interval) {
        UserWatch& w = *e.watch_;
        if (!w.automatic || w.detached || w.running || !e.snapshot_) return false;
        FileVersion found;
        FolderVersion foundEhnd;
        const bool user = !e.userFile_.empty() && w.watch.Due(e.userFile_, now, interval, found);
        const bool ehnd = !e.ehndDir_.empty() && w.ehnd.Due(e.ehndDir_, now, interval, foundEhnd);
        if (!user && !ehnd) return false;
        w.worker.Join();
        const std::uint64_t ticket = ++e.loads_;
        // The thread reaches the engine only through target, which the engine clears
        // before it goes; it never needs the engine to wait for it.
        w.running = w.worker.Start([target = w.target, ticket, dirs = e.dirs_, verify = e.verifyDicts_,
                                    file = e.userFile_, ehndDir = e.ehndDir_,
                                    cacheWrite = e.cacheWrite_ == CacheWrite::Sync] {
            const FileVersion version = VersionOf(file);
            dict::LoadResult r = dict::LoadFolders(dirs, {}, verify, cacheWrite, file);
            FilterLoad filters = FiltersOf(r, ehndDir);
            std::lock_guard<std::mutex> owner(target->mu);
            if (!target->engine) return;
            Engine& engine = *target->engine;
            std::lock_guard<std::mutex> lock(engine.mu_);
            Finish(engine, ticket, version, std::move(r), false, {}, std::move(filters));
            engine.watch_->running = false;
            engine.watch_->done.notify_all();
        });
        return w.running;
    }

    // Takes the engine's current dictionaries into a session that holds others.
    static void Refresh(const Engine& e, std::shared_ptr<const dict::Snapshot>& snap,
                        std::shared_ptr<Generated>& generated, std::shared_ptr<const filter::Filters>& filters,
                        std::unique_ptr<SessionState>& state) {
        std::lock_guard<std::mutex> lock(e.mu_);
        StartReload(e, std::chrono::steady_clock::now(), kCheckInterval);
        if (snap == e.snapshot_ && filters == e.filters_) return;
        snap = e.snapshot_;
        generated = e.generated_;
        filters = e.filters_;
        state = std::make_unique<SessionState>();
    }

    static bool CheckNow(const Engine& e, bool wait) {
        std::unique_lock<std::mutex> lock(e.mu_);
        UserWatch& w = *e.watch_;
        w.done.wait(lock, [&w] { return !w.running; });
        w.watch.next = {};
        w.ehnd.next = {};
        if (!StartReload(e, std::chrono::steady_clock::now(), kCheckInterval)) return false;
        if (wait) w.done.wait(lock, [&w] { return !w.running; });
        return true;
    }

    static std::vector<dict::LoadError> Errors(const Engine& e) {
        std::lock_guard<std::mutex> lock(e.mu_);
        return e.watch_->errors;
    }

    static void Current(const Engine& e, std::shared_ptr<const dict::Snapshot>& snap,
                        std::shared_ptr<Generated>& generated, std::shared_ptr<const filter::Filters>& filters) {
        std::lock_guard<std::mutex> lock(e.mu_);
        snap = e.snapshot_;
        generated = e.generated_;
        filters = e.filters_;
    }

    static bool OpenEhnd(Engine& e, const std::string& dir) {
        for (;;) {
            std::shared_ptr<const dict::Snapshot> snap;
            {
                std::lock_guard<std::mutex> lock(e.mu_);
                if (!e.snapshot_) {
                    e.ehndDir_ = dir;
                    return true;
                }
                snap = e.snapshot_;
            }
            FilterLoad filters = LoadFilters(snap.get(), dir);
            if (!filters.read) return false;
            std::lock_guard<std::mutex> lock(e.mu_);
            if (snap != e.snapshot_) continue;
            // Loads under way read the folder before; they are dropped when they finish.
            e.current_ = ++e.loads_;
            e.ehndDir_ = dir;
            e.filters_ = std::move(filters.filters);
            e.filterErrors_ = std::move(filters.errors);
            e.watch_->ehnd.loaded = std::move(filters.version);
            e.watch_->ehnd.hasFailed = false;
            return true;
        }
    }
};


bool CheckUserFile(const Engine& e) { return Access::CheckNow(e, true); }

bool StartUserFileCheck(const Engine& e) { return Access::CheckNow(e, false); }

std::vector<dict::LoadError> UserFileErrors(const Engine& e) { return Access::Errors(e); }

// The text of a load error.
std::string ErrorText(const dict::LoadError& e) {
    std::string s = e.file;
    if (e.line > 0) s += ":" + std::to_string(e.line);
    s += ": ";
    if (!e.message.empty()) return s + e.message;
    switch (e.kind) {
    case dict::LoadErrorKind::Folder: return s + "cannot list the folder";
    case dict::LoadErrorKind::Read: return s + "cannot read the file";
    case dict::LoadErrorKind::Tsv: return s + "malformed line";
    default: return s + "does not load";
    }
}

}  // namespace internal

Engine::Engine(bool reference, CacheWrite cacheWrite, UserReload userReload)
    : reference_(reference), cacheWrite_(cacheWrite), watch_(std::make_unique<internal::UserWatch>()) {
    watch_->automatic = userReload == UserReload::Auto;
    watch_->target->engine = this;
}

Engine::~Engine() {
    {
        std::lock_guard<std::mutex> lock(watch_->target->mu);
        watch_->target->engine = nullptr;
    }
    watch_->worker.Join();
}

void Engine::DetachReload() {
    {
        std::lock_guard<std::mutex> lock(mu_);
        watch_->detached = true;
        watch_->worker.Detach();
    }
    std::lock_guard<std::mutex> lock(watch_->target->mu);
    watch_->target->engine = nullptr;
}

std::string Engine::UserFileError() const {
    std::lock_guard<std::mutex> lock(mu_);
    std::string s;
    for (const dict::LoadError& e : watch_->errors) s += (s.empty() ? "" : "\n") + internal::ErrorText(e);
    if (s.empty() && watch_->watch.hasFailed) s = userFile_ + ": does not load";
    return s;
}

bool Engine::Open(const std::vector<std::string>& dirs, bool verifyDicts, std::string_view userFile) {
    if (!internal::RulesLoaded()) return false;
    return internal::Access::Load(*this, dirs, verifyDicts, std::string(userFile), true);
}

bool Engine::Reload() {
    std::vector<std::string> dirs;
    bool verify;
    std::string userFile;
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (!snapshot_) return false;
        dirs = dirs_;
        verify = verifyDicts_;
        userFile = userFile_;
    }
    return internal::Access::Load(*this, dirs, verify, userFile, false);
}

bool Engine::OpenEhnd(std::string_view dir) { return internal::Access::OpenEhnd(*this, std::string(dir)); }

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

Session::Session(const Engine& engine, bool reference, bool cg, bool misread)
    : engine_(engine), reference_(engine.reference_ || reference), cg_(cg), misread_(misread),
      state_(std::make_unique<internal::SessionState>()) {
    std::lock_guard<std::mutex> lock(engine.mu_);
    snapshot_ = engine.snapshot_;
    generated_ = engine.generated_;
    filters_ = engine.filters_;
}

Session::~Session() = default;

std::string Session::Translate(std::string_view text) {
    if (engine_.watch_->automatic) internal::Access::Refresh(engine_, snapshot_, generated_, filters_, state_);
    if (!snapshot_) return {};
    std::vector<std::string> lines;
    for (const std::string_view line : text::Lines(text)) lines.emplace_back(line);
    internal::TranslationOptions options;
    options.reference = reference_;
    options.wordImprovements.cgRules = cg_;
    options.wordImprovements.misreadFilter = misread_;
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
