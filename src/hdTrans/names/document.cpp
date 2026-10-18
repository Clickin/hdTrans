#include "hdTrans/names/document.h"

#include <algorithm>
#include <cstddef>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "hdTrans/names/chars.h"
#include "hdTrans/names/historical.h"
#include "hdTrans/names/labels.h"
#include "hdTrans/names/readings.h"
#include "hdTrans/names/registry.h"
#include "hdTrans/pipeline/normalize.h"
#include "hdTrans/dict/builtin.h"
#include "hdTrans/userdict/userdict.h"

namespace hdTrans::pipeline {

namespace {

// The titles that show a name to be a person when they follow it (霧島さん, 千歳お姉, 加賀改二).
const std::vector<std::string_view> kPersonTitles = {"さん", "ちゃん", "くん", "君", "様", "さま", "先輩", "先生", "殿", "氏",
                                                     "たん", "っち", "姉", "兄", "お姉", "お兄", "改"};

// Whether key occurs in text with a person title after it or 艦 before it.
bool Titled(std::string_view text, std::string_view key) {
    for (std::size_t i = 0;;) {
        const std::size_t at = text.find(key, i);
        if (at == std::string_view::npos) return false;
        std::string_view before = text.substr(0, at);
        if (before.ends_with("、")) before.remove_suffix(std::string_view("、").size());
        const std::string_view after = text.substr(at + key.size());
        if (before.ends_with("艦")) return true;
        for (const std::string_view t : kPersonTitles) {
            if (after.starts_with(t)) return true;
        }
        i = at + key.size();
    }
}

std::string_view BaseName(std::string_view path) {
    const std::size_t slash = path.find_last_of('/');
    return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

// The codes of the entries compiled into codes get their with-XXX and plain views, as the
// snapshot's own codes do.
void FinishCodes(dict::CodeTable& codes) {
    for (int32_t c = 0; c < static_cast<int32_t>(codes.Size()); c++) {
        codes.SetWithXXX(c);
        codes.SetAsPlain(c);
    }
}

// Splits text at "\n", keeping the empty lines that end with a newline out.
std::vector<std::string_view> Lines(std::string_view text) {
    std::vector<std::string_view> out;
    while (!text.empty()) {
        const std::size_t nl = text.find('\n');
        out.push_back(text.substr(0, nl));
        text = nl == std::string_view::npos ? std::string_view() : text.substr(nl + 1);
    }
    return out;
}

// Lines of lines.tsv: the whole-line rows, JP TAB KR (builtin).
std::map<std::string, std::string> WholeLineRows() {
    std::map<std::string, std::string> rows;
    const std::string text = dict::BuiltinText("lines.tsv");
    for (const std::string_view raw : Lines(text)) {
        const std::string_view line = names::internal::TrimCR(raw);
        const std::size_t tab = line.find('\t');
        if (tab == std::string_view::npos) continue;
        std::string_view kr = line.substr(tab + 1);
        const std::size_t next = kr.find('\t');
        if (next != std::string_view::npos) kr = kr.substr(0, next);
        const std::string_view jp = line.substr(0, tab);
        if (jp.empty() || jp.starts_with("//")) continue;
        rows[std::string(jp)] = std::string(kr);
    }
    return rows;
}

// The line of text with the whole-line row replaced in place (wholeLine): the trimmed part
// of line is a row of lines.tsv (white space around it is kept).
std::optional<std::string> WholeLineOf(const std::map<std::string, std::string>& rows, std::string_view line) {
    const std::string_view t = names::internal::TrimLineSpace(line);
    const auto it = rows.find(std::string(t));
    if (it == rows.end()) return std::nullopt;
    const std::size_t at = line.find(t);
    std::string out(line.substr(0, at));
    out += it->second;
    out += line.substr(at + t.size());
    return out;
}

}  // namespace

struct DocumentData {
    explicit DocumentData(const dict::Snapshot& snap) : registry(&snap), wholeLines(WholeLineRows()) {
        for (const dict::File& f : snap.PlainFiles()) {
            if (BaseName(f.name) == "readings.tsv") registry.LoadReadings(f.text);
        }
        for (const dict::UserEntry& e : snap.TextPlain().Entries()) {
            if (e.file < 0) continue;
            dictKeys.insert(e.key);
            if (e.name) registry.SetName(e.key, e.target);
        }
    }

    names::Registry registry;
    dict::KeySet dictKeys;
    std::map<std::string, std::string> wholeLines;
};

namespace internal {

std::set<std::string> VerseLines(std::span<const std::string> lines) {
    std::set<std::string> verse;
    for (std::size_t i = 0; i + 1 < lines.size(); i++) {
        const std::string_view t = names::internal::TrimSpace(lines[i]);
        if ((t.ends_with("て") || t.ends_with("で")) && !names::internal::TrimSpace(lines[i + 1]).empty()) {
            std::string_view line = lines[i];
            if (line.ends_with('\r')) line.remove_suffix(1);
            verse.insert(std::string(line));
        }
    }
    return verse;
}

std::vector<std::string> PersonPlaces(const dict::Snapshot& snap, std::string_view text, names::Registry* registry,
                                      const dict::KeySet* persons) {
    std::vector<std::string> cues = names::SpeakerCues(text);
    std::vector<std::string> out;
    for (const std::string& k : snap.PersonPlaceKeys(text)) {
        const bool named = registry != nullptr && (registry->Named(k) || (persons != nullptr && persons->count(k) > 0));
        const bool cued = std::find(cues.begin(), cues.end(), k) != cues.end();
        if (named || Titled(text, k) || cued) out.push_back(k);
    }
    return out;
}

}  // namespace internal

std::vector<std::string> CuedNames(const dict::Snapshot& snap, std::string_view text) {
    std::vector<std::string> keys;
    for (const std::string& key : names::SpeakerCues(text)) {
        if (std::find(keys.begin(), keys.end(), key) == keys.end() && snap.PersonCueKey(key)) keys.push_back(key);
    }
    return keys;
}

std::unique_ptr<DocumentContext> BuildDocument(const dict::Snapshot& snap, std::span<const std::string> lines) {
    auto doc = std::make_unique<DocumentContext>();
    doc->snap = &snap;
    doc->data = &snap.DocumentData();
    doc->codes = std::make_unique<dict::CodeTable>();
    doc->registry = std::make_unique<names::Registry>(doc->data->registry);
    names::Registry& r = *doc->registry;
    for (const std::string& line : lines) r.Add(line);

    doc->registryKey = r.Key();
    doc->female = internal::FemaleSpeech(lines);
    doc->key = doc->registryKey;
    if (doc->female) doc->key += std::string("\0female", 7);

    dict::KeySet persons;
    for (const std::string& line : lines) {
        for (const std::string& k : internal::PersonPlaces(snap, line, nullptr, nullptr)) persons.insert(k);
    }
    doc->persons.assign(persons.begin(), persons.end());
    std::sort(doc->persons.begin(), doc->persons.end());
    if (!doc->persons.empty()) {
        doc->key += std::string("\0persons\t", 9);
        for (std::size_t i = 0; i < doc->persons.size(); i++) {
            if (i > 0) doc->key += "\t";
            doc->key += doc->persons[i];
        }
    }
    doc->persons_ = persons;

    doc->verseLines = internal::VerseLines(lines);
    doc->verse.assign(doc->verseLines.begin(), doc->verseLines.end());
    for (const std::string& line : doc->verse) doc->key += std::string("\0verse\0", 7) + line;

    doc->historical = names::HistoricalNames(snap, lines);
    if (!doc->historical.empty()) doc->key += std::string("\0historical\0", 12) + doc->historical;

    for (const std::string& t : r.Names()) doc->accepted.insert(t);
    doc->named = doc->accepted;
    for (const std::string& t : r.NamedSurfaces()) doc->named.insert(t);

    const std::string registryDict = r.Dict();
    userdict::Context ctx{doc->codes.get(), &doc->families, &snap.Base()};
    doc->extra = userdict::Parse(registryDict, -1, ctx).entries;
    for (dict::UserEntry& e : userdict::Parse(doc->historical, -1, ctx).entries) doc->extra.push_back(std::move(e));
    FinishCodes(*doc->codes);
    return doc;
}

LineFacts ReadLine(DocumentContext& doc, std::string_view line) {
    LineFacts facts;
    if (line.ends_with('\r')) line.remove_suffix(1);
    facts.text = names::StripRuby(pipeline::internal::WriteKinTerms(line, doc.female));
    names::Registry& r = *doc.registry;
    dict::KeySet plain, guarded;
    for (const names::Occurrence& o : r.Occurrences(facts.text)) {
        if (o.noun) {
            guarded.insert(o.surface);
        } else {
            plain.insert(o.surface);
        }
    }
    for (const std::string& t : guarded) {
        if (plain.count(t) == 0) facts.skip.push_back(t);
    }
    std::sort(facts.skip.begin(), facts.skip.end());
    facts.persons = internal::PersonPlaces(*doc.snap, facts.text, doc.registry.get(), &doc.persons_);
    std::sort(facts.persons.begin(), facts.persons.end());
    facts.wholeLine = WholeLineOf(doc.data->wholeLines, facts.text);
    facts.verse = doc.verseLines.count(std::string(line)) > 0;
    return facts;
}

LineLookup LookupOf(DocumentContext& doc, const LineFacts& facts) {
    LineLookup out;
    std::string key;
    for (const std::string& t : facts.skip) {
        key += t;
        key += '\t';
    }
    auto found = doc.plainByKey.find(key);
    if (found == doc.plainByKey.end()) {
        std::vector<dict::UserEntry> entries;
        for (const dict::UserEntry& e : doc.extra) {
            if (!doc.data->dictKeys.contains(e.key) &&
                !std::binary_search(facts.skip.begin(), facts.skip.end(), e.key)) entries.push_back(e);
        }
        found = doc.plainByKey.emplace(key, std::make_unique<dict::UserText>(doc.snap->TextPlain(), std::move(entries))).first;
    }
    out.plain = found->second.get();
    for (const std::string& t : doc.accepted) {
        if (!std::binary_search(facts.skip.begin(), facts.skip.end(), t)) out.confirmed.insert(t);
    }
    out.persons.insert(facts.persons.begin(), facts.persons.end());
    return out;
}

LineContext ContextOf(const DocumentContext& doc, const LineLookup& lookup) {
    LineContext ctx;
    ctx.document = true;
    ctx.female = doc.female;
    ctx.lookup.plain = lookup.plain;
    ctx.lookup.confirmed = &lookup.confirmed;
    ctx.lookup.persons = &lookup.persons;
    ctx.named = &doc.named;
    return ctx;
}

}  // namespace hdTrans::pipeline

namespace hdTrans::dict {

const pipeline::DocumentData& Snapshot::DocumentData() const {
    std::call_once(documentOnce_, [this] {
        documentData_ = std::make_shared<const pipeline::DocumentData>(*this);
    });
    return *documentData_;
}

}  // namespace hdTrans::dict
