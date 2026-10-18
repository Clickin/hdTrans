#pragma once
// The word items of a sentence as the compose stage reads them.
#include <vector>

#include "hdTrans/output/item.h"
#include "hdTrans/word/analyze.h"

namespace hdTrans::internal {

// The items of chain in chain order.
void ResolvedItems(const word::Chain& chain, std::vector<output::ResolvedItem>& out);

}  // namespace hdTrans::internal
