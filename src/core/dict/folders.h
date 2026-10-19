#pragma once
// Program data locations. Paths are UTF-8; names are matched without ASCII case.
#include <string>
#include <string_view>

namespace hdTrans::dict {

// Existing file (or directory) ignoring case, otherwise folder/name.
std::string FindChild(std::string_view folder, std::string_view name, bool directory = false);
// programDir/hdTrans when it is a folder, otherwise programDir.
std::string DataFolder(std::string_view programDir);
// The user directory of the data root.
std::string UserFolder(std::string_view programDir);
std::string ShippedFolder(std::string_view programDir);
// ShippedFolder(programDir) when it is a folder, else dicts in the current directory.
std::string DefaultFolder(std::string_view programDir);
// Files in UserFolder(programDir). UserFile is empty for an unknown programDir.
std::string UserFile(std::string_view programDir);
std::string ConfigFile(std::string_view programDir);
// programDir/Ehnd when present, outside the hdTrans data folder.
std::string EhndFolder(std::string_view programDir);

}  // namespace hdTrans::dict
