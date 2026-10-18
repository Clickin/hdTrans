#pragma once
// The readings of names that the base dictionary lacks or reads otherwise.
#include <map>
#include <string>

namespace hdTrans::names::internal {

// Adds the built-in readings (kana, or Hangul) to readings.
void AddBuiltinReadings(std::map<std::string, std::string>& readings);

}  // namespace hdTrans::names::internal
