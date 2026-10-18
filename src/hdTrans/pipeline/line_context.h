#pragma once
// What the stages of the text path need from the document and the session a line
// belongs to, besides the dictionaries.
#include <span>
#include <string>

#include "hdTrans/dict/snapshot.h"

namespace hdTrans::pipeline {

struct LineContext {
    // The line is a line of a document (its kin terms and ruby markup are rewritten);
    // false for text translated without a document.
    bool document = true;
    // The document shows a woman's speech.
    bool female = false;
    // Host markers: text that is protected wherever it occurs, the longest first.
    std::span<const std::string> markers;
    // The lookups of the line: its plain entries (the plain dictionaries with the names
    // of the document), the name keys confirmed for it and its person places.
    dict::LookupContext lookup;
    // The surfaces the document shows to be names: its accepted names and every
    // surface written where a name stands (a speaker label, before an honorific,
    // under ruby, a part of a ・ chain). Null without a document.
    const dict::KeySet* named = nullptr;
};

}  // namespace hdTrans::pipeline
