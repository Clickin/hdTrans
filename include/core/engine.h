#pragma once
// C++ API of the engine. All strings are UTF-8.
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace hdTrans {

namespace dict {
class Snapshot;
}

namespace filter {
class Filters;
}

namespace internal {
struct Access;
struct Generated;
struct SessionState;
struct UserWatch;
}

// Off never publishes a cache generation; Sync attempts publication before Open returns.
// Off can write a .bad marker and rename a generation proven corrupt.
enum class CacheWrite { Off, Sync };

// Auto checks the user dictionary file when a translation starts, at most once a second.
// A changed file is reloaded with the folders in the background; the new dictionaries
// replace the old ones when ready. A version of the file that fails to load is not
// retried until the file changes again, and the dictionaries before it stay.
enum class UserReload { Off, Auto };

// The loaded dictionaries. Shared by any number of sessions and threads.
class Engine {
public:
    // Reference mode disables translation improvements. Sessions inherit it.
    explicit Engine(bool reference = false, CacheWrite cacheWrite = CacheWrite::Sync,
                    UserReload userReload = UserReload::Off);
    // Waits for a background reload, unless DetachReload was called.
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // Loads the folders in order, later folders overriding earlier ones, then the user
    // dictionary file userFile (none when empty or missing). A user dictionary file that
    // does not load is left out (UserFileError says why). Returns false when no
    // dictionary could be loaded; the dictionaries loaded before stay.
    bool Open(const std::vector<std::string>& dirs, bool verifyDicts = false, std::string_view userFile = {});

    // Reloads the folders and user dictionary file given to Open, and the Ehnd folder,
    // and swaps the new dictionaries in. Sessions keep translating with the dictionaries
    // they started with (see UserReload). Returns false when the load fails; the
    // dictionaries loaded before stay.
    bool Reload();

    // Takes the filters and the user dictionary of the Ehnd folder dir (its
    // PreFilter*.txt, PostFilter*.txt, SkipLayer*.txt and UserDict*.txt files, see
    // docs/ehnd-compat.md) for the sessions created or reset after it, with the
    // prefilter and postfilter files of the dictionary folders; an empty dir drops them.
    // Before Open it only names the folder that Open reads. With UserReload::Auto the
    // folder is watched like the user dictionary file. Returns false when dir cannot be
    // read; the folder taken before stays.
    bool OpenEhnd(std::string_view dir);

    // The lines of the filter files and of the Ehnd folder that the last Open, Reload or
    // OpenEhnd skipped, as "file:line: message".
    std::vector<std::string> FilterErrors() const;

    bool loaded() const;

    // Why the last version of the user dictionary file that was read did not load, one
    // line per error; empty when it loaded or there is none.
    std::string UserFileError() const;

    // Stops automatic reloads and lets a running one finish on its own, so that the
    // destructor does not wait for it; for a library being unloaded.
    void DetachReload();

    // The current dictionaries; null before a successful Open.
    std::shared_ptr<const dict::Snapshot> Dictionaries() const;
    // The current filters; null without filter files and Ehnd folder.
    std::shared_ptr<const filter::Filters> Filters() const;

private:
    friend class Session;
    friend struct internal::Access;
    const bool reference_;
    const CacheWrite cacheWrite_;
    mutable std::mutex mu_;
    std::vector<std::string> dirs_;
    bool verifyDicts_ = false;
    std::string userFile_;
    std::shared_ptr<const dict::Snapshot> snapshot_;
    std::shared_ptr<internal::Generated> generated_;  // the marker families made on snapshot_
    mutable std::uint64_t loads_ = 0;  // loads started
    std::uint64_t current_ = 0;        // the load that made snapshot_
    std::unique_ptr<internal::UserWatch> watch_;  // the user dictionary file state
    std::string ehndDir_;
    std::shared_ptr<const filter::Filters> filters_;
    std::vector<std::string> filterErrors_;
};

// A translation with memory: names and other context found in earlier text of the
// session shape the translation of later text. Text may hold one or more
// "\n"-separated lines ("\r" is kept). One session is used from one thread at a time.
// A session translates with the dictionaries of the engine when it was created or last
// reset; with UserReload::Auto, a translation that finds new dictionaries in the engine
// takes them and first forgets what the session learned, as Reset does.
class Session {
public:
    explicit Session(const Engine& engine, bool reference = false, bool cg = true, bool misread = false);
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    // Lines over 64 KiB are translated in pieces of at most 16 KiB, split at sentence ends outside brackets where possible.
    std::string Translate(std::string_view text);

    // Forgets everything learned from earlier text and takes the engine's current
    // dictionaries.
    void Reset();

private:
    const Engine& engine_;
    const bool reference_;
    const bool cg_;
    const bool misread_;
    std::shared_ptr<const dict::Snapshot> snapshot_;
    std::shared_ptr<internal::Generated> generated_;
    std::shared_ptr<const filter::Filters> filters_;
    std::unique_ptr<internal::SessionState> state_;
};

}  // namespace hdTrans
