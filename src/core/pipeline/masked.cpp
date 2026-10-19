#include <string>
#include <string_view>
#include <vector>

#include "core/dict/builtin.h"
#include "core/dict/tsv.h"
#include "core/output/pieces.h"
#include "core/pipeline/runes.h"
#include "core/pipeline/sound.h"
#include "core/text/hangul.h"
#include "core/word/kana.h"

namespace hdTrans::pipeline::internal {

namespace {

bool IsKanaRune(char32_t r) { return IsHiraganaRune(r) || IsKatakanaRune(r); }

// The masks a word may be transliterated around; × and ✕ join names (A×B) and are left out.
bool TransliteratedMask(char32_t r) { return IsMaskRune(r) && r != U'×' && r != U'✕'; }

// A row of masked.tsv cut at its mask, the kana in katakana; koAfter ends in a particle
// mark when the Korean ends in the mask.
struct MaskedRow {
    std::u32string before, after;
    std::u32string koBefore, koAfter;
};

// The rows of masked.tsv.
std::vector<MaskedRow> BuildRows() {
    std::vector<MaskedRow> rows;
    const std::string table = dict::BuiltinText("masked.tsv");
    dict::TsvReader reader(table, "//");
    while (reader.Next()) {
        const auto& f = reader.Fields();
        if (f.size() < 3 || f[0].empty()) continue;
        const std::u32string ja = ToKatakana(DecodeText(f[0])), ko = DecodeText(f[1]), word = DecodeText(f[2]);
        const size_t j = ja.find(U'○'), k = ko.find(U'○');
        if (j == std::u32string::npos || k == std::u32string::npos || word.empty()) continue;
        MaskedRow row{ja.substr(0, j), ja.substr(j + 1), ko.substr(0, k), ko.substr(k + 1)};
        if (row.koAfter.empty()) {
            const int final = text::Final(word.back());
            row.koAfter = final == text::kFinalNone    ? output::kVowelMark
                          : final == text::kFinalRieul ? output::kRieulMark
                                                       : output::kConsonantMark;
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

const std::vector<MaskedRow>& Rows() {
    static const std::vector<MaskedRow> rows = BuildRows();
    return rows;
}

class MaskedText {
public:
    MaskedText(std::string_view text, const LineContext& ctx, const dict::Snapshot& snap)
        : ctx_(ctx), snap_(snap), rs_(DecodeText(text)), f_(ToKatakana(rs_)) {}

    std::string Write();

private:
    bool Covered(size_t start, size_t end) const;
    bool Fragment(size_t start, size_t end) const;
    bool Row(size_t i, size_t j, size_t& start, size_t& end, std::u32string& korean) const;
    bool Transliterated(size_t i, size_t j, size_t done, size_t& start, size_t& end, std::u32string& korean) const;

    const LineContext& ctx_;
    const dict::Snapshot& snap_;
    std::u32string rs_;
    std::u32string f_;  // rs_ in katakana
};

// Whether a dictionary key that is not a kana spelling covers characters [start, end).
bool MaskedText::Covered(size_t start, size_t end) const {
    const std::string s = EncodeText(std::u32string_view(rs_).substr(start, end - start));
    const dict::Candidates rd = snap_.GetWordInfo(s, dict::LookupMode::General, ctx_.lookup);
    for (const dict::Candidate& c : rd.list) {
        if ((c.user == nullptr || !c.user->kana) && c.source.size() >= s.size()) return true;
    }
    return false;
}

// The longest row around the mask run [i, j): its kana before and after the mask match,
// a row with nothing on one side of its mask matching only where no kana stands there.
bool MaskedText::Row(size_t i, size_t j, size_t& start, size_t& end, std::u32string& korean) const {
    const std::u32string_view f = f_, mask = std::u32string_view(rs_).substr(i, j - i);
    const MaskedRow* best = nullptr;
    for (const MaskedRow& r : Rows()) {
        if (r.before.size() > i || !f.substr(0, i).ends_with(r.before) || !f.substr(j).starts_with(r.after)) continue;
        const size_t s = i - r.before.size(), e = j + r.after.size();
        if (r.before.empty() && s > 0 && IsKanaRune(rs_[s - 1])) continue;
        if (r.after.empty() && e < rs_.size() && IsKanaRune(rs_[e])) continue;
        if (best != nullptr && r.before.size() + r.after.size() <= best->before.size() + best->after.size()) continue;
        best = &r;
        start = s;
        end = e;
    }
    if (best == nullptr) return false;
    korean = best->koBefore + std::u32string(mask) + best->koAfter;
    return true;
}

// Whether the katakana [start, end) are one syllable that is no dictionary key: a piece of
// a word that a mask cuts (オ, ワ, ガン; not キー).
bool MaskedText::Fragment(size_t start, size_t end) const {
    size_t syllables = 0;
    for (size_t k = start; k < end; k++) {
        if (!RuneIn(rs_[k], U"ャュョァィゥェォヮーッン")) syllables++;
    }
    if (syllables != 1) return false;
    const std::string s = EncodeText(std::u32string_view(rs_).substr(start, end - start));
    for (const dict::Candidate& c : snap_.GetWordInfo(s, dict::LookupMode::General, ctx_.lookup).list) {
        if (c.source.size() == s.size()) return false;
    }
    return true;
}

// The katakana word around the mask run [i, j), no earlier than done, written in Hangul
// with its masks kept: katakana on both sides of each mask, a syllable after each, and a
// fragment (Fragment) next to the first mask (オ○ニ 오○니, ワ○ピース 와○피스; not
// ポケ○ン, キー○ボタン or ヨド○シカメラ).
bool MaskedText::Transliterated(size_t i, size_t j, size_t done, size_t& start, size_t& end,
                                std::u32string& korean) const {
    const std::u32string_view rs = rs_;
    if (!TransliteratedMask(rs[i]) || i == done || !IsKatakanaRune(rs[i - 1]) || j >= rs.size() || !IsKatakanaRune(rs[j])) {
        return false;
    }
    start = i;
    while (start > done && IsKatakanaRune(rs[start - 1])) start--;
    size_t after = j;
    while (after < rs.size() && IsKatakanaRune(rs[after])) after++;
    if (!Fragment(start, i) && !Fragment(j, after)) return false;
    end = j;
    while (end < rs.size() && (IsKatakanaRune(rs[end]) || TransliteratedMask(rs[end]))) end++;
    while (TransliteratedMask(rs[end - 1])) end--;
    korean.clear();
    for (size_t k = start; k < end;) {
        size_t n = k;
        const bool kana = IsKatakanaRune(rs[k]);
        while (n < end && IsKatakanaRune(rs[n]) == kana) n++;
        if (kana) {
            if (k > start && RuneIn(rs[k], U"ンッーァィゥェォャュョヮ")) return false;
            std::string hangul;
            if (!word::TransliterateName(EncodeText(rs.substr(k, n - k)), hangul) || hangul.empty()) return false;
            korean += DecodeText(hangul);
        } else {
            korean += rs.substr(k, n - k);
        }
        k = n;
    }
    return true;
}

std::string MaskedText::Write() {
    std::string out;
    size_t done = 0;
    for (size_t i = 0; i < rs_.size();) {
        if (!IsMaskRune(rs_[i])) {
            i++;
            continue;
        }
        size_t j = i;
        while (j < rs_.size() && rs_[j] == rs_[i]) j++;
        size_t start = 0, end = 0;
        std::u32string korean;
        const bool found = (Row(i, j, start, end, korean) && start >= done) || Transliterated(i, j, done, start, end, korean);
        if (!found || Covered(start, end)) {
            i = j;
            continue;
        }
        out += EncodeText(std::u32string_view(rs_).substr(done, start - done));
        out += EncodeText(korean);
        done = i = end;
    }
    if (done == 0) return EncodeText(rs_);
    return out + EncodeText(std::u32string_view(rs_).substr(done));
}

}  // namespace

std::string WriteMasked(std::string_view text, const LineContext& ctx, const dict::Snapshot& snap) {
    for (const char32_t r : DecodeText(text)) {
        if (IsMaskRune(r)) return MaskedText(text, ctx, snap).Write();
    }
    return std::string(text);
}

}  // namespace hdTrans::pipeline::internal
