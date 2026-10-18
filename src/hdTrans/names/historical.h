#pragma once
// The premodern names of a document: the names of persons written with titles and
// court offices (朽木左兵衛尉成綱, 上総介三郎平朝臣信長), read into plain-dictionary rows.
#include <span>
#include <string>

#include "hdTrans/dict/snapshot.h"

namespace hdTrans::names {

// The rows JP TAB KR TAB name, one per line and sorted by JP, of the premodern names
// in lines. Among the readings of a surface found in several runs, the longest run wins.
std::string HistoricalNames(const dict::Snapshot& snap, std::span<const std::string> lines);

}  // namespace hdTrans::names
