// snapshot_check: dumps the dictionary lookups over corpus lines in a line-oriented
// tab-separated form, for comparison with a reference dump; or measures loading.
//
//   snapshot_check -dicts DIR[,DIR...] -entries DIR -raw FILE -each FILE [-eachlines N] [-out FILE]
//   snapshot_check -dicts DIR[,DIR...] -time
//
// -entries names a folder with entries-raw.tsv and entries-text.tsv: compiled plain
// entries, one per line (key target pos sem gram prio file line order flags negative).
// Pass a looks every position of every line up in all modes with the text entries;
// pass b in modes 0 and 3 with the raw entries, strategy UserFirst, prior dictionary
// BZ, the name keys of the line confirmed, its person places and an entry filter.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#ifdef __APPLE__
#include <malloc/malloc.h>
#endif

#include "hdTrans/dict/snapshot.h"
#include "hdTrans/dict/tsv.h"
#include "hdTrans/text/utf8.h"

using namespace hdTrans::dict;

namespace {

std::vector<std::string> Split(std::string_view s, char sep) {
    std::vector<std::string> out;
    for (;;) {
        const std::size_t i = s.find(sep);
        out.emplace_back(s.substr(0, i));
        if (i == std::string_view::npos) return out;
        s.remove_prefix(i + 1);
    }
}

bool ReadAll(const std::string& path, std::string& out) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    char buf[1 << 16];
    for (;;) {
        const std::size_t n = std::fread(buf, 1, sizeof buf, f);
        out.append(buf, n);
        if (n < sizeof buf) break;
    }
    std::fclose(f);
    return true;
}

std::vector<std::string> ReadLines(const std::string& path, long limit) {
    std::string text;
    if (!ReadAll(path, text)) {
        std::fprintf(stderr, "cannot read %s\n", path.c_str());
        std::exit(1);
    }
    std::vector<std::string> out;
    for (std::string& l : Split(text, '\n')) {
        if (limit >= 0 && static_cast<long>(out.size()) == limit) break;
        if (!l.empty() && l.back() == '\r') l.pop_back();
        out.push_back(std::move(l));
    }
    if (!out.empty() && out.back().empty()) out.pop_back();
    return out;
}

struct Entries {
    CodeTable codes;
    std::unique_ptr<UserText> text;
};

std::unique_ptr<Entries> ReadEntries(const std::string& path) {
    std::string data;
    if (!ReadAll(path, data)) {
        std::fprintf(stderr, "cannot read %s\n", path.c_str());
        std::exit(1);
    }
    auto out = std::make_unique<Entries>();
    std::vector<UserEntry> list;
    for (const std::string& line : Split(data, '\n')) {
        if (line.empty()) continue;
        const std::vector<std::string> f = Split(line, '\t');
        if (f.size() != 11) {
            std::fprintf(stderr, "bad entry line: %s\n", line.c_str());
            std::exit(1);
        }
        UserEntry e;
        e.key = f[0];
        e.target = f[1];
        e.codes = out->codes.At(out->codes.Intern(f[2], f[3], f[4], "GN"));
        e.prio = std::atoi(f[5].c_str());
        e.file = std::atoi(f[6].c_str());
        e.line = std::atoi(f[7].c_str());
        e.order = std::atoi(f[8].c_str());
        std::string reg;
        for (const char c : f[9]) {
            switch (c) {
            case 'N': e.name = true; break;
            case 'L': e.nameLayer = true; break;
            case 'K': e.kana = true; break;
            case 'F': e.force = true; break;
            case 'H': e.hidden = true; break;
            case 'O': e.honorific = true; break;
            case 'E': e.end = true; break;
            case 'P': e.polite = true; break;
            case 'S': e.stative = true; break;
            case 'v': case 'c': case 'l': e.finalClass = c; break;
            default: reg += c;
            }
        }
        if (!reg.empty()) e.registerLevel = static_cast<uint8_t>(std::atoi(reg.c_str()));
        e.negative = f[10];
        list.push_back(std::move(e));
    }
    out->text = std::make_unique<UserText>(std::move(list));
    return out;
}

class Writer {
public:
    explicit Writer(std::FILE* f) : f_(f) {}
    ~Writer() { Flush(); }
    Writer& operator<<(std::string_view s) {
        buf_.append(s);
        if (buf_.size() > (1 << 20)) Flush();
        return *this;
    }
    Writer& operator<<(long v) { return *this << std::string_view(std::to_string(v)); }
    void Flush() {
        std::fwrite(buf_.data(), 1, buf_.size(), f_);
        buf_.clear();
    }

private:
    std::FILE* f_;
    std::string buf_;
};

std::string EntryFlags(const UserEntry& u) {
    std::string s;
    const std::pair<bool, char> flags[] = {{u.name, 'N'},   {u.nameLayer, 'L'}, {u.kana, 'K'},
                                           {u.force, 'F'},  {u.hidden, 'H'},    {u.honorific, 'O'},
                                           {u.end, 'E'},    {u.polite, 'P'},    {u.stative, 'S'}};
    for (const auto& [on, c] : flags) {
        if (on) s += c;
    }
    if (u.finalClass) s += u.finalClass;
    if (u.registerLevel) s += std::to_string(u.registerLevel);
    return s;
}

void Lookup(Writer& w, const std::string& pos, const char* pass, int mode, const Candidates& c) {
    if (c.list.empty() && c.count == 0) return;
    w << "W\t" << pos << "\t" << pass << "\t" << long{mode} << "\t" << long{c.count} << "\n";
    for (const Candidate& x : c.list) {
        w << "\t" << x.source << "\t" << x.target << "\t" << x.codes->partText << "\t" << x.codes->sem.Text() << "\t"
          << x.codes->gram.Text() << "\t" << x.codes->dict.Text() << "\t" << long{x.rule} << "\t" << long{x.id} << "\t";
        if (const UserEntry* u = x.user) {
            w << "u:" << long{u->prio} << ":" << long{u->file} << ":" << long{u->line} << ":" << long{u->order} << ":"
              << EntryFlags(*u);
        } else {
            w << "-";
        }
        w << "\n";
    }
}

template <class R>
void Records(Writer& w, const char* kind, const std::string& pos, std::string_view key, const std::vector<R>& records) {
    w << kind << "\t" << pos << "\t" << key << "\n";
    for (const R& r : records) {
        w << "\t" << r.target << "\t" << r.codes->partText << "\t" << r.codes->sem.Text() << "\t" << r.codes->gram.Text()
          << "\t" << r.codes->dict.Text() << "\t" << long{r.rule} << "\t" << long{r.id} << "\n";
    }
}

std::string Join(const std::vector<std::string>& v) {
    std::string s;
    for (std::size_t i = 0; i < v.size(); i++) {
        if (i) s += "|";
        s += v[i];
    }
    return s;
}

// The substrings of line of 2 to 6 characters that have name-layer records, in order
// of first occurrence.
std::vector<std::string> NameKeys(const Snapshot& s, const std::string& line) {
    std::vector<std::string> keys;
    KeySet seen;
    for (std::size_t i = 0; i < line.size(); i += hdTrans::text::DecodeRune(std::string_view(line).substr(i)).size) {
        std::size_t end = i;
        for (int n = 1; n <= 6 && end < line.size(); n++) {
            end += hdTrans::text::DecodeRune(std::string_view(line).substr(end)).size;
            const std::string k = line.substr(i, end - i);
            if (n >= 2 && !seen.count(k) && !s.NameRecords(k).empty()) {
                seen.insert(k);
                keys.push_back(k);
            }
        }
    }
    return keys;
}

void Exact(Writer& w, const Snapshot& s, const std::string& pos, std::string_view q) {
    std::size_t end = 0;
    for (int n = 1; n <= 8 && end < q.size(); n++) {
        end += hdTrans::text::DecodeRune(q.substr(end)).size;
        const std::string_view k = q.substr(0, end);
        if (const auto c = s.RetrieveRecords(k, LookupMode::General); !c.empty()) Records(w, "R", pos, k, c);
        if (const auto r = s.NameRecords(k); !r.empty()) Records(w, "N", pos, k, r);
        if (const std::string p = s.PersonName(k); !p.empty()) w << "P\t" << pos << "\t" << k << "\t" << p << "\n";
        if (s.PersonCueKey(k)) w << "Q\t" << pos << "\t" << k << "\n";
    }
}

bool SkipB(const void*, const UserEntry& e) { return e.kana || (e.nameLayer && e.line % 3 == 0); }

std::size_t HeapInUse() {
#ifdef __APPLE__
    malloc_statistics_t st;
    malloc_zone_statistics(nullptr, &st);
    return st.size_in_use;
#else
    return 0;
#endif
}

int Time(const std::vector<std::string>& dirs) {
    std::vector<double> times;
    std::size_t before = HeapInUse();
    std::size_t heap = 0, bytes = 0;
    for (int i = 0; i < 5; i++) {
        const auto start = std::chrono::steady_clock::now();
        LoadResult r = LoadFolders(dirs);
        times.push_back(std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
        if (!r.snapshot) {
            std::fprintf(stderr, "load failed\n");
            return 1;
        }
        heap = HeapInUse() - before;
        bytes = r.snapshot->MemoryBytes();
    }
    std::sort(times.begin(), times.end());
    std::printf("load min %.3fs median %.3fs\nheap in use after load %.1f MB, snapshot %.1f MB\n", times[0], times[2],
                static_cast<double>(heap) / 1e6, static_cast<double>(bytes) / 1e6);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    std::string dicts, entriesDir, raw, each, outPath;
    long eachLines = 10000;
    bool timing = false;
    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        const bool hasValue = i + 1 < argc;
        if (a == "-dicts" && hasValue) dicts = argv[++i];
        else if (a == "-entries" && hasValue) entriesDir = argv[++i];
        else if (a == "-raw" && hasValue) raw = argv[++i];
        else if (a == "-each" && hasValue) each = argv[++i];
        else if (a == "-eachlines" && hasValue) eachLines = std::atol(argv[++i]);
        else if (a == "-out" && hasValue) outPath = argv[++i];
        else if (a == "-time") timing = true;
        else {
            std::fprintf(stderr, "usage: snapshot_check -dicts DIR[,DIR] (-time | -entries DIR -raw FILE -each FILE [-eachlines N] [-out FILE])\n");
            return 2;
        }
    }
    const std::vector<std::string> dirs = Split(dicts, ',');
    if (timing) return Time(dirs);

    const LoadResult loaded = LoadFolders(dirs);
    if (!loaded.snapshot) {
        std::fprintf(stderr, "load failed\n");
        return 1;
    }
    const Snapshot& s = *loaded.snapshot;
    std::FILE* outFile = outPath.empty() ? stdout : std::fopen(outPath.c_str(), "wb");
    if (!outFile) return 1;
    {
        Writer w(outFile);
        for (const LoadError& e : loaded.errors) {
            w << "E\t" << e.file << "\t" << long{static_cast<int>(e.kind)} << "\t" << long{e.line} << "\n";
        }
        for (const File& f : s.PlainFiles()) {
            const std::size_t slash = f.name.find_last_of("/\\");
            w << "F\t" << (slash == std::string::npos ? f.name : f.name.substr(slash + 1)) << "\n";
        }
        for (const std::string& x : s.Warnings()) w << "X\t" << x << "\n";
        w << "S\tbase " << static_cast<long>(s.Base().Len()) << "\tuser "
          << static_cast<long>(s.User() ? s.User()->Len() : 0) << "\n";

        const auto rawEntries = ReadEntries(entriesDir + "/entries-raw.tsv");
        const auto textEntries = ReadEntries(entriesDir + "/entries-text.tsv");
        LookupContext a;
        a.plain = textEntries->text.get();
        a.strategy = Strategy::LongestMatch;
        for (int m = 0; m <= 4; m++) Lookup(w, "sentinel", "a", m, s.GetWordInfo("", static_cast<LookupMode>(m), a));
        for (int m = 1; m <= 3; m++) {
            Records(w, "R", "sentinel", std::to_string(m), s.RetrieveRecords("", static_cast<LookupMode>(m)));
        }

        const struct {
            const char* name;
            std::vector<std::string> lines;
        } sets[] = {{"raw", ReadLines(raw, -1)}, {"each", ReadLines(each, eachLines)}};
        for (const auto& set : sets) {
            for (std::size_t n = 0; n < set.lines.size(); n++) {
                const std::string& line = set.lines[n];
                const std::string at = std::string(set.name) + ":" + std::to_string(n + 1);
                const std::vector<std::string> persons = s.PersonPlaceKeys(line);
                if (!persons.empty()) w << "K\t" << at << "\t" << Join(persons) << "\n";
                const std::vector<std::string> confirmed = NameKeys(s, line);
                if (!confirmed.empty()) w << "C\t" << at << "\t" << Join(confirmed) << "\n";
                const KeySet confirmedSet(confirmed.begin(), confirmed.end());
                const KeySet personSet(persons.begin(), persons.end());
                LookupContext b;
                b.plain = rawEntries->text.get();
                b.strategy = Strategy::UserFirst;
                b.priorDict = "BZ";
                b.confirmed = &confirmedSet;
                b.persons = &personSet;
                b.skip = {SkipB, nullptr};
                for (std::size_t i = 0; i < line.size(); i += hdTrans::text::DecodeRune(std::string_view(line).substr(i)).size) {
                    const std::string_view q = std::string_view(line).substr(i);
                    const std::string pos = at + ":" + std::to_string(i);
                    for (int m = 0; m <= 4; m++) Lookup(w, pos, "a", m, s.GetWordInfo(q, static_cast<LookupMode>(m), a));
                    for (const int m : {0, 3}) Lookup(w, pos, "b", m, s.GetWordInfo(q, static_cast<LookupMode>(m), b));
                    Exact(w, s, pos, q);
                }
            }
        }
    }
    if (outFile != stdout) std::fclose(outFile);
    return 0;
}
