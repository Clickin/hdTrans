// Generated table; do not edit.
#include "hdTrans/output/markers.h"

namespace hdTrans::output {

std::string_view MarkerFamilyLetters(char marker) {
    switch (marker) {
        case '&': return "ACEFHJKLMN";
        case '$': return "ACDFGIJKL";
        case '%': return "ABCDE";
    }
    return {};
}

}  // namespace hdTrans::output
