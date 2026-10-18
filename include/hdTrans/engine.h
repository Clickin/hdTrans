#pragma once
// C++ API of the engine. All strings are UTF-8.
#include <string>
#include <string_view>
#include <vector>

namespace hdTrans {

// The loaded dictionaries. Shared by any number of sessions and threads.
class Engine {
public:
    // Loads the folders in order; later folders override earlier ones. Returns false when
    // no dictionary could be loaded.
    bool Open(const std::vector<std::string>& dirs);

    // Reloads the folders given to Open. Sessions keep translating with the dictionaries
    // they started with.
    bool Reload();

    bool loaded() const { return loaded_; }

private:
    std::vector<std::string> dirs_;
    bool loaded_ = false;
};

// A translation with memory: names and other context found in earlier text of the
// session shape the translation of later text. Text may hold one or more
// "\n"-separated lines ("\r" is kept). One session is used from one thread at a time.
class Session {
public:
    explicit Session(const Engine& engine) : engine_(engine) {}

    std::string Translate(std::string_view text);

    // Forgets everything learned from earlier text.
    void Reset();

private:
    const Engine& engine_;
};

}  // namespace hdTrans
