#pragma once
// The files of an Ehnd folder (docs/ehnd-compat.md).
#include <string>
#include <string_view>
#include <vector>

#include "core/filter/filter.h"

namespace hdTrans::filter {

// A file of an Ehnd folder: its kind, name and path.
struct EhndFile {
    Source::Kind kind;
    std::string name, path;
};

// The PreFilter*.txt, PostFilter*.txt, SkipLayer*.txt and UserDict*.txt files of the
// folder dir in load order: by kind in this order, then by name compared without ASCII
// case. false when dir cannot be listed.
bool EhndFiles(std::string_view dir, std::vector<EhndFile>& files);

// Appends the files of the Ehnd folder dir (EhndFiles) to sources, decoded by EhndText.
// false, appending nothing, when dir cannot be listed or a file cannot be read.
bool ReadEhndFolder(std::string_view dir, std::vector<Source>& sources, std::vector<Error>& errors);

// The UTF-8 text of the bytes of a file: UTF-16LE after its byte order mark, otherwise
// UTF-8 (a UTF-8 mark is dropped), with CR LF as LF.
std::string EhndText(std::string_view bytes);

// An entry of a UserDict file.
struct UserRow {
    std::string key, target, attr;
    bool phrase = false;  // TYPE 0 or 2: a fixed expression (A9D0); otherwise a noun (I110)
};

// The entries of the UserDict text of file. Lines without a key, and lines whose key or
// Korean takes more than 30 bytes or whose attribute takes more than 36 in a double-byte
// code page, are listed in errors.
std::vector<UserRow> ParseUserDict(std::string_view text, std::string_view file, std::vector<Error>& errors);

}  // namespace hdTrans::filter
