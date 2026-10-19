#pragma once
#include <string>
#include <vector>

namespace hdTrans::frontend {

// Base data metadata only; user dictionaries and filters do not change it.
std::string DictionaryVersion(const std::vector<std::string>& directories);

}  // namespace hdTrans::frontend
