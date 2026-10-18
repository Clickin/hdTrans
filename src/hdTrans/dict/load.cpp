#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <system_error>
#include <unordered_set>

#include "hdTrans/dict/layer.h"
#include "hdTrans/dict/names.h"
#include "hdTrans/dict/parallel.h"
#include "hdTrans/dict/snapshot.h"
#include "hdTrans/text/hangul.h"
#include "hdTrans/text/utf8.h"

namespace hdTrans::dict {

namespace {

std::filesystem::path PathOf(std::string_view utf8) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

std::string Utf8Of(const std::filesystem::path& p) {
    const std::u8string s = p.u8string();
    return std::string(reinterpret_cast<const char*>(s.data()), s.size());
}

bool ReadFile(const std::filesystem::path& path, std::string& out) {
#ifdef _WIN32
    std::FILE* f = _wfopen(path.c_str(), L"rb");
#else
    std::FILE* f = std::fopen(path.c_str(), "rb");
#endif
    if (!f) return false;
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (!ec) out.reserve(static_cast<std::size_t>(size));
    char buf[1 << 16];
    bool ok = true;
    for (;;) {
        const std::size_t n = std::fread(buf, 1, sizeof buf, f);
        out.append(buf, n);
        if (n < sizeof buf) {
            ok = std::ferror(f) == 0;
            break;
        }
    }
    std::fclose(f);
    return ok;
}

std::string_view BaseName(std::string_view name) {
    const std::size_t slash = name.find_last_of("/\\");
    return slash == std::string_view::npos ? name : name.substr(slash + 1);
}

// Whether text starts, after blank and "#" lines, with a header line whose first
// column is key.
bool ColumnFile(std::string_view text) {
    for (std::size_t pos = 0, n = 0; pos < text.size(); n++) {
        const std::size_t nl = text.find('\n', pos);
        std::string_view line = text.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos);
        pos = nl == std::string_view::npos ? text.size() : nl + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.remove_suffix(1);
        if (n == 0 && line.starts_with("\xEF\xBB\xBF")) line.remove_prefix(3);
        if (line.empty() || line.starts_with("#")) continue;
        return line.substr(0, line.find('\t')) == "key";
    }
    return false;
}

// Interns the codes of the senses of a load, remembering the codes of each pos value.
class CodeReader {
public:
    explicit CodeReader(CodeTable& codes) : codes_(codes) {}

    // The number of the codes of s; false for an unknown pos.
    bool Code(const Sense& s, int32_t& code) {
        auto it = pos_.find(s.pos);
        if (it == pos_.end()) {
            it = pos_.emplace(std::string(s.pos), PosInfo()).first;
            PosInfo& info = it->second;
            info.known = PosCodes(it->first, info.part, info.sem, info.gram);
        }
        const PosInfo& p = it->second;
        if (!p.known) return false;
        std::string_view sem = s.sem.empty() ? p.sem : s.sem;
        const std::string_view gram = s.gram.empty() ? p.gram : s.gram;
        if (s.xxx && sem.find("XXX") == std::string_view::npos) {
            withXXX_.assign(sem);
            withXXX_ += "XXX";
            sem = withXXX_;
        }
        // Consecutive senses often have the same codes.
        if (sem.data() != withXXX_.data() && lastCode_ >= 0 && p.part == last_[0] && sem == last_[1] &&
            gram == last_[2] && s.dict == last_[3]) {
            code = lastCode_;
            return true;
        }
        code = codes_.Intern(p.part, sem, gram, s.dict);
        if (sem.data() == withXXX_.data()) {
            lastCode_ = -1;
        } else {
            last_[0] = p.part;
            last_[1] = sem;
            last_[2] = gram;
            last_[3] = s.dict;
            lastCode_ = code;
        }
        return true;
    }

private:
    struct PosInfo {
        bool known = false;
        std::string_view part, sem, gram;  // the pos value itself or the tag table
    };

    CodeTable& codes_;
    std::unordered_map<std::string, PosInfo, StringHash, std::equal_to<>> pos_;
    std::string withXXX_;
    std::string_view last_[4];  // the codes of lastCode_, views into the files
    int32_t lastCode_ = -1;
};

// Parses the column file f into layer with reader; adds the indexes of its rows with
// the flag replace to replacing. false on an error other than unknown pos: a file
// with malformed lines fails the load, and its unknown pos lines are not listed.
bool ReadColumnFile(const File& f, CodeReader& reader, internal::Layer& layer, std::vector<uint32_t>* replacing,
                    std::vector<LoadError>& errors) {
    layer.Rows().reserve(layer.Rows().size() + static_cast<std::size_t>(std::count(f.text.begin(), f.text.end(), '\n')) + 1);
    TsvReader rows(f.text);
    SenseReader senses(rows);
    std::vector<LoadError> unknown;
    Sense s;
    while (senses.Next(s)) {
        int32_t code;
        if (!reader.Code(s, code)) {
            unknown.push_back({f.name, LoadErrorKind::UnknownPos, rows.Line(), TsvErrorKind::Read});
            continue;
        }
        for (const std::string_view key : {s.key, s.reading}) {
            if (key.empty()) continue;
            if (s.replace && replacing) replacing->push_back(static_cast<uint32_t>(layer.Rows().size()));
            layer.Add(key, s.ko, code, s.prio, s.rule, s.id);
        }
    }
    if (senses.Errors().empty()) {
        errors.insert(errors.end(), unknown.begin(), unknown.end());
        return true;
    }
    for (const TsvError& e : senses.Errors()) {
        errors.push_back({f.name, e.kind == TsvErrorKind::Read ? LoadErrorKind::Read : LoadErrorKind::Tsv, e.line, e.kind});
    }
    return false;
}

// A name file parsed ahead, with codes numbered in its own table.
struct ParsedNames {
    internal::Layer layer{false};
    std::vector<uint32_t> replacing;
    std::vector<LoadError> errors;
    bool ok = true;
};

}  // namespace

// Builds a Snapshot (Load).
struct SnapshotLoader {
    std::vector<File> files;
    std::vector<LoadError> errors;
    std::shared_ptr<Snapshot> snap{new Snapshot()};
    CodeReader reader{snap->codes_};

    LoadResult Run();
    void Respell(std::string_view key, std::string_view ko);
    std::string PersonSpelling(std::string_view ko, const std::vector<Record>& records) const;
};

void SnapshotLoader::Respell(std::string_view key, std::string_view ko) {
    Snapshot::Respelling sp;
    sp.ko = ko;
    sp.mode = Snapshot::Respelling::Mode::AllNouns;
    const std::size_t keyRunes = text::RuneCount(key);
    for (const Record& r : snap->base_.Records(key)) {
        if (!internal::PlaceSense(r.codes->sem)) continue;
        if (keyRunes <= 2 && text::SameSound(r.target, ko)) {
            sp.mode = Snapshot::Respelling::Mode::SameSound;
            break;
        }
        sp.mode = Snapshot::Respelling::Mode::AllPlaces;
    }
    snap->places_.insert_or_assign(std::string(key), std::move(sp));
}

// The first Korean of a person sense (QPN) among records that differs from ko in
// spelling only: the same sound and as many characters.
std::string SnapshotLoader::PersonSpelling(std::string_view ko, const std::vector<Record>& records) const {
    for (const Record& r : records) {
        if (r.codes->sem.HasPrefix("QPN") && r.target != ko && text::SameSound(r.target, ko) &&
            text::RuneCount(r.target) == text::RuneCount(ko)) {
            return std::string(r.target);
        }
    }
    return {};
}

LoadResult SnapshotLoader::Run() {
    // The name files are parsed while the base dictionary is built; their senses join
    // the name layer one file at a time afterwards, as each is processed.
    std::vector<std::size_t> baseFiles, nameFiles, rest;
    for (std::size_t i = 0; i < files.size(); i++) {
        if (!ColumnFile(files[i].text)) rest.push_back(i);
        else if (BaseName(files[i].name).starts_with("names")) nameFiles.push_back(i);
        else baseFiles.push_back(i);
    }
    std::vector<ParsedNames> parsed(nameFiles.size());
    CodeTable nameCodes;
    internal::Background nameReader([&] {
        CodeReader r(nameCodes);
        for (std::size_t j = 0; j < nameFiles.size(); j++) {
            ParsedNames& p = parsed[j];
            p.ok = ReadColumnFile(files[nameFiles[j]], r, p.layer, &p.replacing, p.errors);
            if (!p.ok) break;
        }
    });

    internal::Layer base(true), user(false), names(false);
    for (const std::size_t i : baseFiles) {
        const std::string_view file = BaseName(files[i].name);
        if (file == "userdict.tsv") {
            const std::size_t start = user.Rows().size();
            if (!ReadColumnFile(files[i], reader, user, nullptr, errors)) return {nullptr, std::move(errors)};
            user.ByPrio(start);
        } else {
            const std::size_t start = base.Rows().size();
            std::vector<uint32_t> replacing;
            if (!ReadColumnFile(files[i], reader, base, &replacing, errors)) return {nullptr, std::move(errors)};
            base.Shadow(start, file.starts_with("words") ? &replacing : nullptr, snap->codes_);
            base.Compact();
        }
    }
    Snapshot& s = *snap;
    s.hasDictionary_ = rest.size() < files.size();
    if (s.hasDictionary_) {
        s.user_ = user.Build(s.codes_);
        s.base_ = base.Build(s.codes_);
    }
    for (const std::size_t i : baseFiles) std::string().swap(files[i].text);
    nameReader.Wait();
    std::vector<int32_t> nameCode(nameCodes.Size());
    for (std::size_t c = 0; c < nameCode.size(); c++) {
        const Codes* x = nameCodes.At(static_cast<int32_t>(c));
        nameCode[c] = s.codes_.Intern(x->partText, x->sem.Text(), x->gram.Text(), x->dict.Text());
    }
    for (std::size_t j = 0; j < nameFiles.size(); j++) {
        const std::size_t i = nameFiles[j];
        ParsedNames& p = parsed[j];
        errors.insert(errors.end(), p.errors.begin(), p.errors.end());
        if (!p.ok) return {nullptr, std::move(errors)};
        const std::size_t start = names.Rows().size();
        for (internal::Row& r : p.layer.Rows()) r.code = nameCode[static_cast<std::size_t>(r.code)];
        std::vector<uint32_t> replacing = std::move(p.replacing);
        for (uint32_t& at : replacing) at += static_cast<uint32_t>(start);
        names.Append(std::move(p.layer));
        std::unordered_set<std::string_view> own;
        for (const uint32_t at : replacing) {
            const internal::Row& r = names.Rows()[at];
            own.insert(r.key);
            Respell(r.key, r.target);
        }
        auto& rows = names.Rows();
        for (std::size_t k = start; k < rows.size(); k++) {
            const internal::Row& r = rows[k];
            if (own.count(r.key)) continue;
            const Snapshot::Respelling* sp = s.Place(r.key);
            if (sp && !sp->ko.empty() && !s.codes_.At(r.code)->sem.HasPrefix("QPN")) rows[k].code = internal::Layer::kDeleted;
        }
        names.Compact();
        names.ByPrio(start);
        internal::NameKeyCounts parts[4], dropped;
        internal::ParallelFor(rows.size() - start, [&](std::size_t part, std::size_t begin, std::size_t end) {
            parts[part] = internal::DropNameKeys(rows.data() + start + begin, end - begin, s.base_);
        });
        for (const auto& p : parts) {
            dropped.punct += p.punct;
            dropped.inflected += p.inflected;
            dropped.longKorean += p.longKorean;
        }
        if (dropped.punct + dropped.inflected + dropped.longKorean > 0) {
            s.warnings_.push_back(files[i].name + ": name senses dropped: " + std::to_string(dropped.punct) +
                                  " keys with 、 or 。, " + std::to_string(dropped.inflected) + " inflected keys, " +
                                  std::to_string(dropped.longKorean) + " long Korean");
        }
        names.Shadow(start, nullptr, s.codes_);
        names.Compact();
    }
    s.names_ = names.Build(s.codes_);
    for (auto& [key, sp] : s.places_) {
        sp.person = PersonSpelling(sp.ko, s.base_.Records(key));
        if (sp.person.empty()) sp.person = PersonSpelling(sp.ko, s.names_.Records(key));
    }
    // The codes of the lookups' rewritten candidates.
    for (int32_t c = 0; c < static_cast<int32_t>(s.codes_.Size()); c++) {
        s.codes_.SetWithXXX(c);
        s.codes_.SetAsPlain(c);
    }
    for (const std::size_t i : rest) s.plain_.push_back(std::move(files[i]));
    files.clear();
    return {std::move(snap), std::move(errors)};
}

LoadResult Load(std::vector<File> files) {
    SnapshotLoader loader;
    loader.files = std::move(files);
    return loader.Run();
}

LoadResult LoadFolders(const std::vector<std::string>& folders) {
    std::vector<File> files;
    std::vector<LoadError> errors;
    for (std::size_t f = 0; f < folders.size(); f++) {
        const std::filesystem::path dir = PathOf(folders[f]);
        std::vector<std::string> names;
        std::error_code ec;
        for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
            const std::string name = Utf8Of(it->path().filename());
            std::error_code fileEc;
            if (name.ends_with(".tsv") && it->is_regular_file(fileEc)) names.push_back(name);
        }
        if (ec) {
            errors.push_back({folders[f], LoadErrorKind::Folder, 0, TsvErrorKind::Read});
            return {nullptr, std::move(errors)};
        }
        std::sort(names.begin(), names.end());
        for (const std::string& name : names) {
            File file;
            file.folder = static_cast<int>(f);
            file.name = Utf8Of(dir / PathOf(name));
            if (!ReadFile(dir / PathOf(name), file.text)) {
                errors.push_back({file.name, LoadErrorKind::Read, 0, TsvErrorKind::Read});
                return {nullptr, std::move(errors)};
            }
            files.push_back(std::move(file));
        }
    }
    return Load(std::move(files));
}

}  // namespace hdTrans::dict
