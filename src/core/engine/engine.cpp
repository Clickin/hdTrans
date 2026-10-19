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
#include "core/output/families.h"
#include "core/userdict/conjugating.h"
#include "core/text/lines.h"

namespace hdTrans {

namespace {

constexpr std::chrono::seconds kCheckInterval{1};

bool Loaded(const dict::LoadResult& r) { return r.snapshot && r.snapshot->HasDictionary(); }

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
                       std::vector<dict::LoadError> userErrors) {
        if (ticket < e.current_) return Loaded(r);
        UserWatch& w = *e.watch_;
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
        return true;
    }

    // Loads dirs and userFile and makes them current unless a load started later already
    // did. With open, a user dictionary file that keeps the folders from loading is left
    // out, and on success dirs and userFile become the engine's.
    static bool Load(Engine& e, const std::vector<std::string>& dirs, bool verify, const std::string& userFile,
                     bool open) {
        std::uint64_t ticket;
        {
            std::lock_guard<std::mutex> lock(e.mu_);
            ticket = ++e.loads_;
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
        std::lock_guard<std::mutex> lock(e.mu_);
        if (!Finish(e, ticket, version, std::move(r), bare, std::move(userErrors))) return false;
        if (open) {
            e.dirs_ = dirs;
            e.verifyDicts_ = verify;
            e.userFile_ = userFile;
        }
        return true;
    }

    // Starts a background reload when the user dictionary file is due at now; e.mu_ is
    // held.
    static bool StartReload(const Engine& e, std::chrono::steady_clock::time_point now,
                            std::chrono::steady_clock::duration interval) {
        UserWatch& w = *e.watch_;
        FileVersion found;
        if (!w.automatic || w.detached || w.running || !e.snapshot_ || e.userFile_.empty() ||
            !w.watch.Due(e.userFile_, now, interval, found)) {
            return false;
        }
        w.worker.Join();
        const std::uint64_t ticket = ++e.loads_;
        // The thread reaches the engine only through target, which the engine clears
        // before it goes; it never needs the engine to wait for it.
        w.running = w.worker.Start([target = w.target, ticket, dirs = e.dirs_, verify = e.verifyDicts_,
                                    file = e.userFile_, cacheWrite = e.cacheWrite_ == CacheWrite::Sync] {
            const FileVersion version = VersionOf(file);
            dict::LoadResult r = dict::LoadFolders(dirs, {}, verify, cacheWrite, file);
            std::lock_guard<std::mutex> owner(target->mu);
            if (!target->engine) return;
            Engine& engine = *target->engine;
            std::lock_guard<std::mutex> lock(engine.mu_);
            Finish(engine, ticket, version, std::move(r), false, {});
            engine.watch_->running = false;
            engine.watch_->done.notify_all();
        });
        return w.running;
    }

    // Takes the engine's current dictionaries into a session that holds others.
    static void Refresh(const Engine& e, std::shared_ptr<const dict::Snapshot>& snap,
                        std::shared_ptr<Generated>& generated, std::unique_ptr<SessionState>& state) {
        std::lock_guard<std::mutex> lock(e.mu_);
        StartReload(e, std::chrono::steady_clock::now(), kCheckInterval);
        if (snap == e.snapshot_) return;
        snap = e.snapshot_;
        generated = e.generated_;
        state = std::make_unique<SessionState>();
    }

    static bool CheckNow(const Engine& e, bool wait) {
        std::unique_lock<std::mutex> lock(e.mu_);
        UserWatch& w = *e.watch_;
        w.done.wait(lock, [&w] { return !w.running; });
        w.watch.next = {};
        if (!StartReload(e, std::chrono::steady_clock::now(), kCheckInterval)) return false;
        if (wait) w.done.wait(lock, [&w] { return !w.running; });
        return true;
    }

    static std::vector<dict::LoadError> Errors(const Engine& e) {
        std::lock_guard<std::mutex> lock(e.mu_);
        return e.watch_->errors;
    }

    static void Current(const Engine& e, std::shared_ptr<const dict::Snapshot>& snap,
                        std::shared_ptr<Generated>& generated) {
        std::lock_guard<std::mutex> lock(e.mu_);
        snap = e.snapshot_;
        generated = e.generated_;
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
    if (engine_.watch_->automatic) internal::Access::Refresh(engine_, snapshot_, generated_, state_);
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
    state_ = std::make_unique<internal::SessionState>();
    snapshot_ = engine_.snapshot_;
    generated_ = engine_.generated_;
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
