#pragma once
// Where a program finds its dictionaries. Paths are UTF-8.
#include <string>
#include <string_view>

namespace hdTrans::dict {

// The shipped dictionary folder of a program in programDir: programDir/dicts.
std::string ShippedFolder(std::string_view programDir);
// ShippedFolder(programDir) when it is a folder, else dicts in the current directory.
std::string DefaultFolder(std::string_view programDir);
// The user dictionary file of a program in programDir: programDir/userdict.tsv; empty
// for an empty programDir.
std::string UserFile(std::string_view programDir);

}  // namespace hdTrans::dict
