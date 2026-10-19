#include "core/engine.h"

#include <chrono>
#include <cstddef>
#include <memory>

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

}  // namespace

namespace internal {

FileVersion VersionOf(std::string_view utf8Path) {
    FileVersion v;
    v.present = !utf8Path.empty() && dict::internal::ReadStamp(utf8Path, v.stamp);
    if (!v.present) v.stamp = {};
    return v;
}

bool FileWatch::Due(std::string_view path, std::chrono::steady_clock::time_point now,
                    std::chrono::steady_clock::duration interval, FileVersion& found) {
    if (now < next) return false;
    next = now + interval;
    found = VersionOf(path);
    return found != loaded && !(hasFailed && found == failed);
}

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

    // Loads dirs and userFile and makes them current unless a load started later already
    // did; with open, they become the engine's folders and user dictionary file.
    static bool Load(Engine& e, const std::vector<std::string>& dirs, bool verify, const std::string& userFile,
                     bool open) {
        std::uint64_t ticket;
        {
            std::lock_guard<std::mutex> lock(e.mu_);
            ticket = ++e.loads_;
        }
        const FileVersion version = VersionOf(userFile);
        dict::LoadResult r = dict::LoadFolders(dirs, {}, verify, e.cacheWrite_ == CacheWrite::Sync, userFile);
        const bool ok = r.snapshot && r.snapshot->HasDictionary();
        std::lock_guard<std::mutex> lock(e.mu_);
        if (ticket < e.current_) return ok;
        UserWatch* w = e.watch_.get();
        if (!ok) {
            if (w) {
                w->watch.failed = version;
                w->watch.hasFailed = true;
                w->errors = std::move(r.errors);
            }
            return false;
        }
        e.current_ = ticket;
        if (open) {
            e.dirs_ = dirs;
            e.verifyDicts_ = verify;
            e.userFile_ = userFile;
        }
        e.snapshot_ = std::move(r.snapshot);
        e.generated_ = NewGenerated(*e.snapshot_);
        if (w) {
            w->watch.loaded = version;
            w->watch.hasFailed = false;
            w->errors.clear();
        }
        return true;
    }

    // Starts a background reload when the user dictionary file is due at now; e.mu_ is
    // held.
    static bool StartReload(const Engine& e, std::chrono::steady_clock::time_point now,
                            std::chrono::steady_clock::duration interval) {
        UserWatch* w = e.watch_.get();
        FileVersion found;
        if (!w || w->running || !e.snapshot_ || e.userFile_.empty() || !w->watch.Due(e.userFile_, now, interval, found)) {
            return false;
        }
        if (w->worker.joinable()) w->worker.join();
        w->running = true;
        w->worker = std::thread([w, dirs = e.dirs_, verify = e.verifyDicts_, file = e.userFile_] {
            Load(*w->owner, dirs, verify, file, false);
            std::lock_guard<std::mutex> lock(w->owner->mu_);
            w->running = false;
            w->done.notify_all();
        });
        return true;
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

    static bool CheckNow(const Engine& e) {
        std::unique_lock<std::mutex> lock(e.mu_);
        UserWatch* w = e.watch_.get();
        if (!w) return false;
        w->done.wait(lock, [w] { return !w->running; });
        w->watch.next = {};
        if (!StartReload(e, std::chrono::steady_clock::now(), kCheckInterval)) return false;
        w->done.wait(lock, [w] { return !w->running; });
        return true;
    }

    static std::vector<dict::LoadError> Errors(const Engine& e) {
        std::lock_guard<std::mutex> lock(e.mu_);
        return e.watch_ ? e.watch_->errors : std::vector<dict::LoadError>();
    }

    static void Current(const Engine& e, std::shared_ptr<const dict::Snapshot>& snap,
                        std::shared_ptr<Generated>& generated) {
        std::lock_guard<std::mutex> lock(e.mu_);
        snap = e.snapshot_;
        generated = e.generated_;
    }
};


bool CheckUserFile(const Engine& e) { return Access::CheckNow(e); }

std::vector<dict::LoadError> UserFileErrors(const Engine& e) { return Access::Errors(e); }

}  // namespace internal

Engine::Engine(bool reference, CacheWrite cacheWrite, UserReload userReload)
    : reference_(reference), cacheWrite_(cacheWrite) {
    if (userReload == UserReload::Auto) {
        watch_ = std::make_unique<internal::UserWatch>();
        watch_->owner = this;
    }
}

Engine::~Engine() {
    if (watch_ && watch_->worker.joinable()) watch_->worker.join();
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
    if (engine_.watch_) internal::Access::Refresh(engine_, snapshot_, generated_, state_);
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
