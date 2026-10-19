// The codes and rule references of the dictionary files (*.tsv with a column header).
#include <algorithm>
#include <cstdio>
#include <filesystem>

#include "core/dict/codes.h"
#include "rulec/rulec.h"

namespace hdTrans::rulec {

namespace {

bool ReadFile(const std::string& path, std::string& out) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) return false;
    char buf[1 << 16];
    for (std::size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) out.append(buf, n);
    std::fclose(f);
    return true;
}

std::vector<std::string_view> Split(std::string_view line) {
    std::vector<std::string_view> out;
    for (std::size_t start = 0;;) {
        const std::size_t tab = line.find('\t', start);
        out.push_back(line.substr(start, tab == std::string_view::npos ? std::string_view::npos : tab - start));
        if (tab == std::string_view::npos) return out;
        start = tab + 1;
    }
}

void AddChunks(std::set<std::string>& set, std::string_view codes, std::size_t width) {
    for (std::size_t i = 0; i + width <= codes.size(); i += width) set.emplace(codes.substr(i, width));
}

void LoadText(std::string_view text, Inventory& inv) {
    std::vector<std::string_view> header;
    int key = -1, pos = -1, sem = -1, gram = -1, rule = -1;
    for (std::size_t start = 0; start < text.size();) {
        std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = text.substr(start, end - start);
        start = end + 1;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.empty() || line[0] == '#') continue;
        const auto fields = Split(line);
        if (header.empty()) {
            header = fields;
            for (int i = 0; i < static_cast<int>(fields.size()); i++) {
                const std::string_view f = fields[static_cast<std::size_t>(i)];
                if (f == "key") key = i;
                if (f == "pos") pos = i;
                if (f == "sem") sem = i;
                if (f == "gram") gram = i;
                if (f == "rule") rule = i;
            }
            if (key < 0 || pos < 0) return;
            continue;
        }
        auto at = [&](int i) { return i >= 0 && static_cast<std::size_t>(i) < fields.size() ? fields[static_cast<std::size_t>(i)] : std::string_view(); };
        std::string_view part, defSem, defGram;
        bool ending = false;
        if (dict::PosCodes(at(pos), part, defSem, defGram)) {
            ending = dict::PartCode(part).ClassDigit() == '5';
            inv.partCodes.emplace(dict::PartCode(part).Code());
            AddChunks(inv.semCodes, defSem, 3);
            AddChunks(inv.gramCodes, defGram, 2);
        }
        AddChunks(inv.semCodes, at(sem), 3);
        AddChunks(inv.gramCodes, at(gram), 2);
        int number = 0;
        for (const char c : at(rule)) {
            if (c < '0' || c > '9' || number > 100000000) {
                number = -1;
                break;
            }
            number = number * 10 + (c - '0');
        }
        if (number > 0) {
            inv.ruleRows[number]++;
            (ending ? inv.ruleEndingKeys : inv.ruleKeys)[number].emplace(at(key));
        }
    }
}

}  // namespace

bool LoadInventory(const std::string& dictDir, Inventory& inv, std::string& error) {
    std::error_code ec;
    std::vector<std::string> paths;
    for (std::filesystem::directory_iterator it(dictDir, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->path().extension() == ".tsv") paths.push_back(it->path().string());
    }
    if (ec) {
        error = dictDir + ": " + ec.message();
        return false;
    }
    std::sort(paths.begin(), paths.end());
    for (const std::string& p : paths) {
        std::string text;
        if (!ReadFile(p, text)) {
            error = p + ": cannot read";
            return false;
        }
        LoadText(text, inv);
    }
    inv.loaded = true;
    return true;
}

}  // namespace hdTrans::rulec
