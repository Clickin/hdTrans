#pragma once
// C++ API of the engine. All strings are UTF-8.
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
}

// Off never publishes a cache generation; Sync attempts publication before Open returns.
// Off can write a .bad marker and rename a generation proven corrupt.
enum class CacheWrite { Off, Sync };

// The loaded dictionaries. Shared by any number of sessions and threads.
class Engine {
public:
    // Reference mode disables translation improvements. Sessions inherit it.
    explicit Engine(bool reference = false, CacheWrite cacheWrite = CacheWrite::Sync)
        : reference_(reference), cacheWrite_(cacheWrite) {}

    // Loads the folders in order; later folders override earlier ones. Returns false when
    // no dictionary could be loaded; the dictionaries loaded before stay.
    bool Open(const std::vector<std::string>& dirs, bool verifyDicts = false);

    // Reloads the folders given to Open, and the Ehnd folder, and swaps the new
    // dictionaries in. Sessions keep translating with the dictionaries they started
    // with. Returns false when the load fails; the dictionaries loaded before stay.
    bool Reload();

    // Takes the filters and the user dictionary of the Ehnd folder dir (its
    // PreFilter*.txt, PostFilter*.txt, SkipLayer*.txt and UserDict*.txt files, see
    // docs/ehnd-compat.md) for the sessions created or reset after it, with the
    // prefilter and postfilter files of the dictionary folders; an empty dir drops them.
    // Returns false when dir cannot be read; the folder taken before stays.
    bool OpenEhnd(std::string_view dir);

    // The lines of the filter files and of the Ehnd folder that the last Open, Reload or
    // OpenEhnd skipped, as "file:line: message".
    std::vector<std::string> FilterErrors() const;

    bool loaded() const;

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
    std::shared_ptr<const dict::Snapshot> snapshot_;
    std::shared_ptr<internal::Generated> generated_;  // the marker families made on snapshot_
    std::string ehndDir_;
    std::shared_ptr<const filter::Filters> filters_;
    std::vector<std::string> filterErrors_;
};

// A translation with memory: names and other context found in earlier text of the
// session shape the translation of later text. Text may hold one or more
// "\n"-separated lines ("\r" is kept). One session is used from one thread at a time.
// A session translates with the dictionaries of the engine when it was created or last
// reset.
class Session {
public:
    explicit Session(const Engine& engine, bool reference = false);
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
    std::shared_ptr<const dict::Snapshot> snapshot_;
    std::shared_ptr<internal::Generated> generated_;
    std::shared_ptr<const filter::Filters> filters_;
    std::unique_ptr<internal::SessionState> state_;
};

}  // namespace hdTrans
