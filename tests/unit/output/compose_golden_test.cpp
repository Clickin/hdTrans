// The compose stage against the stage records of the expectation sets: for every
// sentence of $HDTRANS_GOLDEN/stages/<set>, the items of its S records are composed
// over the tokens of the chunk (tokenized from the IN or CH records) and must give its
// I, K0 and K records. The generated marker families of each set come from
// $HDTRANS_GOLDEN/families/<set>.tsv, the Conjugating calls of that set's run (C:
// lemma, marker, class, target, error) replayed in order, and must equal its rows (G).
// Skipped when the stages folder or its MANIFEST is absent.
//
// Compose reads item fields beyond the 18 columns of S: entry 0 (part code, grammar
// attributes, target), the fixed flag, the ending surface and length, whether the ending
// group comes from a plain dictionary, and whether another entry holds Hangul (columns
// 20-27). Without them they are taken from the L records of the chain before the
// resolve passes where the item is unchanged, and the records are counted but not
// checked. The Wa of the chain tail that turns a following wide space into a space is
// read from the L records.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "hdTrans/output/compose.h"
#include "hdTrans/output/korean.h"
#include "hdTrans/output/text.h"
#include "hdTrans/tokenizer/tokenize.h"
#include "hdtest.h"
#include "stage_records.h"

using namespace hdTrans;
using namespace hdTrans::output;
using namespace stagerecords;

namespace {

int Int(std::string_view s) { return std::atoi(std::string(s).c_str()); }

struct Position {
    int token = 0;
    int offset = 0;
};

Position ParsePosition(std::string_view s) {
    const size_t colon = s.find(':');
    return {Int(s.substr(0, colon)), Int(s.substr(colon + 1))};
}

DetailClass ParseDetail(std::string_view s) {
    if (s == "TVerbWord") return DetailClass::Yougo;
    if (s == "TKeiyoWord") return DetailClass::Keiyo;
    if (s == "TDakeiyoWord") return DetailClass::Dakeiyo;
    return DetailClass::None;
}

// The part code of a record without its major tag.
std::string CodeOf(std::string_view partCode) {
    return partCode.empty() ? std::string() : std::string(partCode.substr(1));
}

// An item of the chain before the resolve passes (L, LC, LD).
struct ChainItem {
    Position at;
    std::string surface;
    std::string endingSurface;
    struct Entry {
        std::string code, gattr, text;
        bool fixed = false;
    };
    std::vector<Entry> entries;
    bool used = false;
};

struct SentenceKey {
    int chunk;
    int sent;
    bool operator<(const SentenceKey& o) const { return chunk != o.chunk ? chunk < o.chunk : sent < o.sent; }
};

struct BlockRecords {
    std::map<SentenceKey, std::vector<ChainItem>> chain;
    std::map<SentenceKey, std::vector<std::string_view>> selected;  // S records
    std::map<SentenceKey, std::vector<std::string_view>> composed;  // I, K0 and K records
    std::map<SentenceKey, std::pair<int, int>> range;               // first and last token, from K0
};

void ReadChain(const std::vector<std::string_view>& records, BlockRecords& b) {
    ChainItem* current = nullptr;
    for (const auto r : records) {
        const auto f = Split(r, '\t');
        if (f[0] == "L") {
            auto& list = b.chain[{Int(f[1]), Int(f[2])}];
            list.push_back({});
            current = &list.back();
            current->at = ParsePosition(f[3]);
            current->surface = UnescapeStageText(f[5]);
            current->endingSurface = UnescapeStageText(f[9]);
        } else if (f[0] == "LC" && current != nullptr) {
            current->entries.push_back(
                {CodeOf(UnescapeStageText(f[5])), UnescapeStageText(f[7]), UnescapeStageText(f[11]), f[10] == "1"});
        }
    }
}

void ReadComposed(const std::vector<std::string_view>& records, BlockRecords& b) {
    for (const auto r : records) {
        const auto f = Split(r, '\t');
        if (f[0] != "I" && f[0] != "K0" && f[0] != "K") continue;
        const SentenceKey key{Int(f[1]), Int(f[2])};
        b.composed[key].push_back(r);
        if (f[0] == "K0") {
            const int token = Int(f[3]);
            auto [it, inserted] = b.range.try_emplace(key, token, token);
            if (!inserted) it->second.second = token;
        }
    }
}

// The resolved item of an S record, with the fields S lacks from the chain item at the
// same position.
void FillItem(std::string_view record, std::vector<ChainItem>* chain, ResolvedItem& it) {
    const auto f = Split(record, '\t');
    const Position at = ParsePosition(f[3]);
    it.tokenIndex = at.token;
    it.offset = at.offset;
    it.length = Int(f[4]);
    it.surface = UnescapeStageText(f[5]);
    it.selected = Int(f[6]);
    it.selectionFlag = Int(f[7]);
    it.detailClass = ParseDetail(f[8]);
    it.detailKind = Int(f[9]);
    it.detailRecord = Int(f[10]);
    it.directIndex = Int(f[11]);
    auto entry = [](std::string_view part, std::string_view gattr, std::string_view target, ResolvedEntry& e) {
        if (part == "-") {
            e = {};
            return;
        }
        e.code = CodeOf(UnescapeStageText(part));
        e.gattr = UnescapeStageText(gattr);
        e.text = UnescapeStageText(target);
    };
    entry(f[12], f[14], f[15], it.entry);
    entry(f[16], f[17], f[18], it.ending);
    it.first = it.selected == 0 ? it.entry : ResolvedEntry{it.entry.code.substr(0, 1), {}, {}};
    it.fixed = false;
    it.endingSurface.clear();
    it.endingLength = 0;
    it.ownedEnding = false;
    it.otherHangul = false;
    if (f.size() >= 27) {
        // Entry 0, fixed, ending surface and length, plain-dictionary ending, Hangul in
        // the other entries.
        if (f[19] != "-") it.first = {CodeOf(UnescapeStageText(f[19])), UnescapeStageText(f[20]), UnescapeStageText(f[21])};
        it.fixed = f[22] == "1";
        it.endingSurface = UnescapeStageText(f[23]);
        it.endingLength = Int(f[24]);
        it.ownedEnding = f[25] == "1";
        it.otherHangul = f[26] == "1";
        return;
    }
    if (chain == nullptr) return;
    ChainItem* match = nullptr;
    for (auto& c : *chain) {
        if (c.used || c.at.token != at.token || c.at.offset != at.offset || c.surface != it.surface) continue;
        match = &c;
        break;
    }
    if (match == nullptr) return;
    match->used = true;
    if (!match->entries.empty()) {
        const auto& e0 = match->entries[0];
        it.fixed = e0.fixed;
        if (it.selected != 0) it.first = {e0.code, e0.gattr, e0.text};
    }
    for (size_t k = 1; k < match->entries.size(); k++) {
        if (static_cast<int>(k) != it.selected && internal::HasSyllable(match->entries[k].text)) it.otherHangul = true;
    }
    it.endingSurface = match->endingSurface;
    it.endingLength = static_cast<int>(it.endingSurface.size());
}

// Whether the chain item that ends the chain at token i (the last one of a token up to
// i) has an entry with the grammar attribute Wa.
bool TailHasWa(const std::vector<ChainItem>* chain, int i) {
    if (chain == nullptr) return false;
    const ChainItem* tail = nullptr;
    for (const auto& c : *chain) {
        if (c.at.token <= i) tail = &c;
    }
    if (tail == nullptr) return false;
    for (const auto& e : tail->entries) {
        if (e.gattr.find("Wa") != std::string::npos) return true;
    }
    return false;
}

struct Counts {
    long long sentences = 0;
    long long iRecords = 0, iDiffering = 0;
    long long k0Records = 0, k0Differing = 0;
    long long kRecords = 0, kDiffering = 0;
    double seconds = 0;
};

void Compare(const std::string& set, std::string_view header, const std::vector<std::string>& got,
             const std::vector<std::string_view>& want, Counts& n, int& shown) {
    // Records of one type, in order.
    for (const std::string_view tag : {"I", "K0", "K"}) {
        std::vector<std::string_view> g, w;
        for (const auto& r : got) {
            if (r.substr(0, r.find('\t')) == tag) g.push_back(r);
        }
        for (const auto r : want) {
            if (r.substr(0, r.find('\t')) == tag) w.push_back(r);
        }
        long long& records = tag == "I" ? n.iRecords : tag == "K0" ? n.k0Records : n.kRecords;
        long long& differing = tag == "I" ? n.iDiffering : tag == "K0" ? n.k0Differing : n.kDiffering;
        records += static_cast<long long>(w.size());
        for (size_t i = 0; i < std::max(g.size(), w.size()); i++) {
            const std::string_view a = i < g.size() ? g[i] : "<none>";
            const std::string_view b = i < w.size() ? w[i] : "<none>";
            if (a == b) continue;
            differing++;
            if (++shown <= 12) {
                hdtest::Fail(__FILE__, __LINE__,
                             set + " " + std::string(header) + ": got " + std::string(a) + " want " + std::string(b));
            }
        }
    }
}

// Replays the Conjugating calls of a set; returns the number of calls and rows that
// differ.
long long ReplayFamilies(const std::string& path, Families& generated, long long& calls) {
    std::string file;
    if (!ReadFile(path, file)) return -1;
    long long differing = 0;
    std::vector<std::pair<std::string, std::string>> rows;
    for (const auto line : Split(file, '\n')) {
        if (line.empty()) continue;
        const auto f = Split(line, '\t');
        if (f[0] == "C" && f.size() == 6) {
            calls++;
            const auto got = Conjugating(f[1], f[2].empty() ? 0 : f[2][0], f[3], generated);
            const bool same = f[5].empty() ? got.target == f[4] : !got.error.empty();
            if (!same) {
                differing++;
                hdtest::Fail(__FILE__, __LINE__, "Conjugating " + std::string(f[1]) + " " + std::string(f[2]) + " " +
                                                     std::string(f[3]) + ": got " + got.target + " / " + got.error +
                                                     " want " + std::string(f[4]) + " / " + std::string(f[5]));
            }
        } else if (f[0] == "G" && f.size() == 3) {
            rows.emplace_back(f[1], f[2]);
        }
    }
    size_t nonEmpty = 0;
    for (const auto& [key, row] : generated.Rows()) nonEmpty += row.empty() ? 0 : 1;
    size_t wantNonEmpty = 0;
    for (const auto& [key, row] : rows) {
        if (row.empty()) continue;
        wantNonEmpty++;
        if (generated.Row(key) != row) {
            differing++;
            hdtest::Fail(__FILE__, __LINE__, "generated row " + key + ": got " + std::string(generated.Row(key)) +
                                                 " want " + row);
        }
    }
    if (nonEmpty != wantNonEmpty) differing++;
    return differing;
}

}  // namespace

HD_TEST(output_compose_golden) {
    const std::string golden = GoldenDir();
    const std::string dir = golden + "/stages/";
    std::error_code ec;
    if (!std::filesystem::exists(dir + "MANIFEST", ec)) {
        std::printf("skip output_compose_golden: no %sMANIFEST\n", dir.c_str());
        return;
    }
    std::vector<std::string> sets;
    for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (name.size() > 6 && name.ends_with(".S.tsv")) sets.push_back(name.substr(0, name.size() - 6));
    }
    std::sort(sets.begin(), sets.end());
    HD_REQUIRE(!sets.empty());

    Counts total;
    long long familyCalls = 0, familyDiffering = 0;
    double corpus2Seconds = 0;
    long long corpus2Sentences = 0;
    int shown = 0;
    bool extended = false;  // the S records carry the compose fields
    Composer composer;
    composer.record = true;
    tokenizer::Tokens tokens;
    std::vector<TokenText> texts;
    std::vector<ResolvedItem> items;
    std::vector<tokenizer::ControlMarker> markers;
    Pieces out;
    std::vector<std::string> got;
    for (const auto& set : sets) {
        const bool raw = set.ends_with(".raw");
        Families generated;
        const long long d = ReplayFamilies(golden + "/families/" + set + ".tsv", generated, familyCalls);
        if (d < 0) {
            hdtest::Fail(__FILE__, __LINE__, "no families/" + set + ".tsv");
            continue;
        }
        familyDiffering += d;
        std::string inFile, lFile, sFile, iFile, kFile;
        HD_REQUIRE(ReadFile(dir + set + (raw ? ".IN.tsv" : ".P.tsv"), inFile));
        HD_REQUIRE(ReadFile(dir + set + ".L.tsv", lFile));
        HD_REQUIRE(ReadFile(dir + set + ".S.tsv", sFile));
        HD_REQUIRE(ReadFile(dir + set + ".I.tsv", iFile));
        HD_REQUIRE(ReadFile(dir + set + ".K.tsv", kFile));
        const auto inBlocks = Blocks(inFile), lBlocks = Blocks(lFile), sBlocks = Blocks(sFile), iBlocks = Blocks(iFile),
                   kBlocks = Blocks(kFile);
        HD_REQUIRE(inBlocks.size() == sBlocks.size() && lBlocks.size() == sBlocks.size() &&
                   iBlocks.size() == sBlocks.size() && kBlocks.size() == sBlocks.size());
        Settings settings;
        settings.cleanup = !raw;
        settings.text = !raw;
        Counts n;
        for (size_t b = 0; b < sBlocks.size(); b++) {
            BlockRecords rec;
            ReadChain(lBlocks[b].records, rec);
            for (const auto r : sBlocks[b].records) {
                const auto f = Split(r, '\t');
                if (f[0] == "S") rec.selected[{Int(f[1]), Int(f[2])}].push_back(r);
            }
            ReadComposed(iBlocks[b].records, rec);
            ReadComposed(kBlocks[b].records, rec);
            // The chunks of the block.
            std::map<int, std::vector<tokenizer::Piece>> chunks;
            for (const auto r : inBlocks[b].records) {
                const auto f = Split(r, '\t');
                if (raw && f[0] == "IN" && f.size() == 2) {
                    auto& p = chunks[0];
                    if (!f[1].empty()) p.push_back({UnescapeStageText(f[1]), 0});
                } else if (!raw && f[0] == "CH" && f.size() == 5) {
                    chunks[Int(f[1])].push_back({UnescapeStageText(f[4]), Int(f[3])});
                }
            }
            int chunk = -1;
            for (const auto& [key, range] : rec.range) {
                if (key.chunk != chunk) {
                    chunk = key.chunk;
                    tokenizer::TokenizeSpans(chunks[chunk], !raw, tokens);
                    texts.resize(tokens.size());
                    for (size_t j = 0; j < tokens.size(); j++) {
                        texts[j].type = tokens[j].type;
                        texts[j].span = tokens[j].span;
                        texts[j].text = tokens.Raw(j);
                        texts[j].spanText = tokens[j].span != 0 ? tokens.Raw(j) : std::string_view();
                    }
                }
                const auto [first, last] = range;
                if (last >= static_cast<int>(tokens.size())) {
                    hdtest::Fail(__FILE__, __LINE__, set + " " + std::string(sBlocks[b].header) + ": token range");
                    continue;
                }
                const auto chainIt = rec.chain.find(key);
                std::vector<ChainItem>* chain = chainIt == rec.chain.end() ? nullptr : &chainIt->second;
                for (int j = first; j <= last; j++) {
                    const auto v = tokens.View(static_cast<size_t>(j), static_cast<size_t>(last));
                    texts[j].type = v.type;
                    texts[j].text = v.text;
                }
                for (int j = first; j < last; j++) {
                    if (tokens.TypeAfter(j) == tokenizer::kTypeJapanese && tokens[j + 1].type == tokenizer::kTypeWideSpace &&
                        TailHasWa(chain, j)) {
                        texts[j + 1].type = tokens[j + 1].type;
                        texts[j + 1].text = " ";
                        j++;
                    }
                }
                if (static_cast<size_t>(last) + 1 < tokens.size()) {
                    const auto v = tokens.View(static_cast<size_t>(last) + 1, static_cast<size_t>(last));
                    texts[last + 1].type = v.type;
                    texts[last + 1].text = v.text;
                }
                markers.clear();
                for (const auto& m : tokens.markers) {
                    if (m.tokenIndex >= first && m.tokenIndex <= last) markers.push_back(m);
                }
                const auto sel = rec.selected.find(key);
                const size_t count = sel == rec.selected.end() ? 0 : sel->second.size();
                items.resize(count);
                for (size_t k = 0; k < count; k++) {
                    FillItem(sel->second[k], chain, items[k]);
                    extended = extended || Split(sel->second[k], '\t').size() >= 27;
                }

                const auto start = std::chrono::steady_clock::now();
                composer.Compose(items, texts, first, last, markers, settings, generated, out);
                n.seconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
                n.sentences++;

                got.clear();
                const std::string cs = std::to_string(key.chunk) + "\t" + std::to_string(key.sent);
                for (auto it = composer.inflected.rbegin(); it != composer.inflected.rend(); ++it) {
                    const ResolvedItem& item = items[it->item];
                    got.push_back("I\t" + cs + "\t" + std::to_string(item.tokenIndex) + ":" +
                                  std::to_string(item.offset) + "\t" + EscapeStageText(it->composed) + "\t" +
                                  EscapeStageText(it->spaced) + "\t" + (it->space ? "1" : "0"));
                }
                for (int j = first; j <= last; j++) {
                    got.push_back("K0\t" + cs + "\t" + std::to_string(j) + "\t" + EscapeStageText(texts[j].text));
                }
                for (size_t p = 0; p < out.size(); p++) {
                    got.push_back("K\t" + cs + "\t" + std::to_string(p) + "\t" +
                                  std::to_string(static_cast<int>(out[p].span)) + "\t" + EscapeStageText(out[p].text));
                }
                if (!extended) shown = 13;  // differences are expected; not shown
                Compare(set, sBlocks[b].header, got, rec.composed[key], n, shown);
            }
        }
        std::printf("output_compose_golden %s: %lld sentences, I %lld/%lld, K0 %lld/%lld, K %lld/%lld differing, "
                    "compose %.3f s\n",
                    set.c_str(), n.sentences, n.iDiffering, n.iRecords, n.k0Differing, n.k0Records, n.kDiffering,
                    n.kRecords, n.seconds);
        if (set == "corpus2.each") {
            corpus2Seconds = n.seconds;
            corpus2Sentences = n.sentences;
        }
        total.sentences += n.sentences;
        total.iRecords += n.iRecords;
        total.iDiffering += n.iDiffering;
        total.k0Records += n.k0Records;
        total.k0Differing += n.k0Differing;
        total.kRecords += n.kRecords;
        total.kDiffering += n.kDiffering;
    }
    std::printf("output_compose_golden: %zu sets, %lld sentences; I %lld records %lld differing, K0 %lld/%lld, K "
                "%lld/%lld; %lld Conjugating calls, %lld differing; corpus2 %lld sentences composed in %.3f s\n",
                sets.size(), total.sentences, total.iRecords, total.iDiffering, total.k0Records, total.k0Differing,
                total.kRecords, total.kDiffering, familyCalls, familyDiffering, corpus2Sentences, corpus2Seconds);
    HD_CHECK_EQ(familyDiffering, 0LL);
    if (!extended) {
        std::printf("output_compose_golden: the S records lack the compose fields (columns 20-27); records not checked\n");
        return;
    }
    HD_CHECK_EQ(total.iDiffering, 0LL);
    HD_CHECK_EQ(total.k0Differing, 0LL);
    HD_CHECK_EQ(total.kDiffering, 0LL);
}
