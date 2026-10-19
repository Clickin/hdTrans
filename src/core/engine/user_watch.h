#pragma once
// The automatic reload of the user dictionary file (UserReload::Auto).
#include <chrono>
#include <condition_variable>
#include <string_view>
#include <thread>
#include <vector>

#include "core/dict/mapped_file.h"
#include "core/dict/snapshot.h"
#include "core/engine.h"

namespace hdTrans::internal {

// What a check finds at a path: whether a regular file is there, and its stamp.
struct FileVersion {
    bool present = false;
    dict::internal::FileStamp stamp;
    bool operator==(const FileVersion&) const = default;
};

FileVersion VersionOf(std::string_view utf8Path);

// When to reload a watched file.
struct FileWatch {
    FileVersion loaded;  // the version the dictionaries hold
    FileVersion failed;  // the last version that failed to load, when hasFailed
    bool hasFailed = false;
    std::chrono::steady_clock::time_point next{};  // the earliest next check

    // Checks path unless the last check was less than interval before now; true when it
    // finds a version other than loaded and failed, stored in found.
    bool Due(std::string_view path, std::chrono::steady_clock::time_point now,
             std::chrono::steady_clock::duration interval, FileVersion& found);
};

// The reload state of an engine, guarded by the engine's mutex.
struct UserWatch {
    Engine* owner = nullptr;
    FileWatch watch;
    std::thread worker;
    bool running = false;
    std::condition_variable done;  // signalled when running turns false
    std::vector<dict::LoadError> errors;  // why the failed version did not load
};

// Checks the user dictionary file of e now, whatever the interval, and waits for the
// reload it starts; false when it starts none.
bool CheckUserFile(const Engine& e);
// The load errors of the version of the user dictionary file that last failed.
std::vector<dict::LoadError> UserFileErrors(const Engine& e);

}  // namespace hdTrans::internal
