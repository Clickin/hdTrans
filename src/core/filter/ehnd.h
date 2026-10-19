#pragma once
// The files of an Ehnd folder (docs/ehnd-compat.md).
#include <string>
#include <string_view>
#include <vector>

#include "core/filter/filter.h"

namespace hdTrans::filter {

// Appends the PreFilter*.txt, PostFilter*.txt, SkipLayer*.txt and UserDict*.txt files of
// the folder dir to sources, each kind in name order compared without ASCII case, as
// NTFS lists them. A file reads as Ehnd opens it: UTF-16LE after its byte order mark,
// otherwise UTF-8 (a UTF-8 mark is dropped), with CR LF as LF. false when dir cannot be
// listed or a file cannot be read.
bool ReadEhndFolder(std::string_view dir, std::vector<Source>& sources, std::vector<Error>& errors);

// The text of a file as Ehnd reads it, in UTF-8.
std::string EhndText(std::string_view bytes);

// An entry of a UserDict file.
struct UserRow {
    std::string key, target, attr;
    bool phrase = false;  // TYPE 0 or 2: a fixed expression (A9D0); otherwise a noun (I110)
};

// The entries of the UserDict text of file. Lines over Ehnd's length limits and lines
// without a key are listed in errors.
std::vector<UserRow> ParseUserDict(std::string_view text, std::string_view file, std::vector<Error>& errors);

}  // namespace hdTrans::filter
