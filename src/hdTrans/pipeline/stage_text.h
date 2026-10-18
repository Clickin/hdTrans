#pragma once
// Text columns of the stage records: one record per line, tab-separated, UTF-8.
#include <string>
#include <string_view>

namespace hdTrans::pipeline {

// s as a text column: \ tab LF CR as \\ \t \n \r; the other C0 controls, DEL and every
// byte of an invalid UTF-8 sequence as \xNN (lower-case hex); everything else as is.
std::string EscapeStageText(std::string_view s);

}  // namespace hdTrans::pipeline
