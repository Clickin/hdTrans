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

namespace internal {
struct Access;
struct Generated;
struct SessionState;
}

// The loaded dictionaries. Shared by any number of sessions and threads.
class Engine {
public:
    // Reference mode disables translation improvements. Sessions inherit it.
    explicit Engine(bool reference = false) : reference_(reference) {}

    // Loads the folders in order; later folders override earlier ones. Returns false when
    // no dictionary could be loaded; the dictionaries loaded before stay.
    bool Open(const std::vector<std::string>& dirs, bool verifyDicts = false);

    // Reloads the folders given to Open and swaps the new dictionaries in. Sessions keep
    // translating with the dictionaries they started with. Returns false when the load
    // fails; the dictionaries loaded before stay.
    bool Reload();

    bool loaded() const;

    // The current dictionaries; null before a successful Open.
    std::shared_ptr<const dict::Snapshot> Dictionaries() const;

private:
    friend class Session;
    friend struct internal::Access;
    const bool reference_;
    mutable std::mutex mu_;
    std::vector<std::string> dirs_;
    std::shared_ptr<const dict::Snapshot> snapshot_;
    std::shared_ptr<internal::Generated> generated_;  // the marker families made on snapshot_
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

    std::string Translate(std::string_view text);

    // Scores candidates without applying their choices. Disabled by default.
    void SetSelectShadow(bool enabled) { selectShadow_ = enabled; }

    // Forgets everything learned from earlier text and takes the engine's current
    // dictionaries.
    void Reset();

private:
    const Engine& engine_;
    const bool reference_;
    std::shared_ptr<const dict::Snapshot> snapshot_;
    std::shared_ptr<internal::Generated> generated_;
    std::unique_ptr<internal::SessionState> state_;
    bool selectShadow_ = false;
};

}  // namespace hdTrans
