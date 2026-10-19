#pragma once
#include <string>
#include <string_view>

#include "core/engine/trace.h"

namespace hdTrans::cli {

inline void JsonString(std::string& out, std::string_view value) {
    constexpr char hex[] = "0123456789abcdef";
    out += '"';
    for (const char raw : value) {
        const auto c = static_cast<unsigned char>(raw);
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                out += "\\u00";
                out += hex[c >> 4];
                out += hex[c & 15];
            } else {
                out += static_cast<char>(c);
            }
        }
    }
    out += '"';
}

inline void JsonSelection(std::string& out, const internal::TraceSelection& selection) {
    out += "\"index\":" + std::to_string(selection.index) + ",\"text\":";
    JsonString(out, selection.text);
    out += ",\"provenance\":";
    JsonString(out, selection.provenance);
    out += ",\"layer\":";
    JsonString(out, selection.layer);
    out += ",\"id\":" + std::to_string(selection.id) + ",\"source\":";
    JsonString(out, selection.source);
}

inline std::string TraceJson(const internal::TranslationTrace& trace) {
    std::string out = "{\"text\":";
    JsonString(out, trace.text);
    out += ",\"choices\":[";
    bool comma = false;
    for (const auto& choice : trace.choices) {
        if (comma) out += ',';
        comma = true;
        out += "{\"line\":" + std::to_string(choice.line) + ",\"chunk\":" + std::to_string(choice.chunk)
            + ",\"sentence\":" + std::to_string(choice.sentence) + ",\"token\":" + std::to_string(choice.token)
            + ",\"start\":" + std::to_string(choice.start) + ",\"surface\":";
        JsonString(out, choice.surface);
        out += ",\"choice\":{";
        JsonSelection(out, choice.choice);
        out += "},\"ending\":";
        if (choice.ending) {
            out += '{';
            JsonSelection(out, choice.ending->choice);
            out += ",\"surface\":";
            JsonString(out, choice.ending->surface);
            out += '}';
        } else {
            out += "null";
        }
        out += '}';
    }
    out += "],\"logs\":[";
    comma = false;
    for (const auto& log : trace.logs) {
        if (comma) out += ',';
        comma = true;
        JsonString(out, log);
    }
    out += "]}";
    return out;
}

}  // namespace hdTrans::cli
