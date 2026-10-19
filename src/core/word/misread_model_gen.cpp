// Generated table; do not edit.
#include "core/word/misread.h"

namespace hdTrans::word {

namespace {

constexpr MisreadWeight kWeights[] = {
    {"", 0},
};

constexpr MisreadModel kModel{kWeights, 0, 1073741824, 0};

}  // namespace

const MisreadModel& BuiltinMisreadModel() { return kModel; }

}  // namespace hdTrans::word
