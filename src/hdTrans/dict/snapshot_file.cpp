#include "hdTrans/dict/snapshot_file.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits>
#include <mutex>
#include <optional>
#include <system_error>
#include <unordered_map>
#include <unordered_set>

#ifdef _WIN32
#include <windows.h>
#endif

#include "hdTrans/dict/integrity.h"
#include "hdTrans/dict/mapped_file.h"
#include "hdTrans/dict/runes.h"

namespace hdTrans::dict {
namespace {

using internal::Crc32c;
using internal::FileStamp;
using Digest = std::array<std::byte, 32>;
using Bytes = std::span<const std::byte>;
constexpr uint32_t kFormatVersion = 1;
constexpr uint32_t kDataVersion = 1;
constexpr std::size_t kHeaderSize = 256;
constexpr std::size_t kDescriptorSize = 64;
constexpr std::size_t kSegmentSize = 256 * 1024;
constexpr uint32_t kManifest = 1, kCodes = 2, kAux = 3;
constexpr uint32_t kTree = 10, kZeros = 11, kLabel = 12, kTerminal = 13, kRank = 14;
constexpr uint32_t kStarts = 15, kCodeRefs = 16, kBlob = 17;
constexpr uint32_t kNoLayer = 3;
constexpr std::string_view kMagic = "HDSNAP\r\n";

bool SafePath(std::string_view path) {
    return path.find('\0') == std::string_view::npos && internal::ValidUtf8(path);
}

std::filesystem::path PathOf(std::string_view s) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size()));
}

std::string Utf8Of(const std::filesystem::path& p) {
    const auto s = p.u8string();
    return {reinterpret_cast<const char*>(s.data()), s.size()};
}

std::optional<std::string> EnvironmentPath(const char* name) {
#ifdef _WIN32
    const auto variable = PathOf(name);
    SetLastError(ERROR_SUCCESS);
    const auto size = GetEnvironmentVariableW(variable.c_str(), nullptr, 0);
    if (!size) return GetLastError() == ERROR_ENVVAR_NOT_FOUND ? std::nullopt : std::optional<std::string>("");
    if (size > static_cast<DWORD>(std::numeric_limits<int>::max())) return std::string();
    std::wstring value(size, L'\0');
    const auto length = GetEnvironmentVariableW(variable.c_str(), value.data(), size);
    if (!length || length >= size) return std::string();
    const int count = static_cast<int>(length);
    const int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), count, nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) return std::string();
    std::string text(static_cast<std::size_t>(bytes), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), count,
                            text.data(), bytes, nullptr, nullptr) != bytes) return std::string();
    return text;
#else
    const char* value = std::getenv(name);
    return value ? std::optional<std::string>(value) : std::nullopt;
#endif
}

std::string DefaultCacheDirectory() {
#ifdef _WIN32
    const auto home = EnvironmentPath("LOCALAPPDATA");
    constexpr auto suffix = "hdtrans/cache";
#elif defined(__APPLE__)
    const auto home = EnvironmentPath("HOME");
    constexpr auto suffix = "Library/Caches/hdtrans";
#else
    if (const auto xdg = EnvironmentPath("XDG_CACHE_HOME"); xdg && !xdg->empty()) {
        return SafePath(*xdg) ? Utf8Of(PathOf(*xdg) / "hdtrans") : std::string();
    }
    const auto home = EnvironmentPath("HOME");
    constexpr auto suffix = ".cache/hdtrans";
#endif
    return home && !home->empty() && SafePath(*home) ? Utf8Of(PathOf(*home) / suffix) : std::string();
}

std::FILE* OpenFile(const std::filesystem::path& path, bool write) {
#ifdef _WIN32
    return _wfopen(path.c_str(), write ? L"wb" : L"rb");
#else
    return std::fopen(path.c_str(), write ? "wb" : "rb");
#endif
}

Bytes ByteView(std::string_view s) { return std::as_bytes(std::span(s.data(), s.size())); }

uint64_t Get(Bytes bytes, std::size_t at, std::size_t width) {
    uint64_t n = 0;
    for (std::size_t i = 0; i < width; i++) n |= uint64_t{std::to_integer<uint8_t>(bytes[at + i])} << (8 * i);
    return n;
}

void Put(std::span<std::byte> bytes, std::size_t at, uint64_t value, std::size_t width) {
    for (std::size_t i = 0; i < width; i++) bytes[at + i] = static_cast<std::byte>(value >> (8 * i));
}

Digest HexDigest(std::string_view hex) {
    Digest d{};
    auto digit = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
    for (std::size_t i = 0; i < d.size(); i++) d[i] = static_cast<std::byte>(digit(hex[2 * i]) * 16 + digit(hex[2 * i + 1]));
    return d;
}

std::string Hex(Bytes bytes) {
    constexpr char digits[] = "0123456789abcdef";
    std::string out;
    for (const auto b : bytes) {
        const auto n = std::to_integer<unsigned>(b);
        out += digits[n >> 4];
        out += digits[n & 15];
    }
    return out;
}

struct Buffer {
    std::vector<std::byte> data;
    bool ok = true;
    void Number(uint64_t n, std::size_t width = 4) {
        if (!ok || (width < 8 && n >= (uint64_t{1} << (8 * width)))) { ok = false; return; }
        const auto at = data.size();
        data.resize(at + width);
        Put(data, at, n, width);
    }
    void Signed(int n) { Number(static_cast<uint32_t>(n)); }
    void Raw(Bytes bytes) { if (ok) data.insert(data.end(), bytes.begin(), bytes.end()); }
    void Text(std::string_view s) { Number(s.size()); Raw(ByteView(s)); }
};

struct Reader {
    Bytes data;
    std::size_t at = 0;
    bool ok = true;
    Bytes Take(std::size_t n) {
        if (!ok || n > data.size() - at) { ok = false; return {}; }
        const auto out = data.subspan(at, n);
        at += n;
        return out;
    }
    uint64_t Number(std::size_t width = 4) {
        const auto bytes = Take(width);
        return ok ? Get(bytes, 0, width) : 0;
    }
    int Signed() { return static_cast<int32_t>(static_cast<uint32_t>(Number())); }
    std::string_view Text() {
        const auto n = Number();
        const auto bytes = Take(static_cast<std::size_t>(n));
        return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
    }
    std::size_t Count(std::size_t minBytes) {
        const auto n = Number();
        if (!ok || n > (data.size() - at) / minBytes) { ok = false; return 0; }
        return static_cast<std::size_t>(n);
    }
    bool Done() const { return ok && at == data.size(); }
};

struct Source {
    uint32_t folder = 0;
    std::string relative;
    FileStamp stamp;
    Digest hash{};
};

bool HashFile(std::string_view name, Digest& out) {
    std::FILE* f = OpenFile(PathOf(name), false);
    if (!f) return false;
    internal::Sha256 hash;
    std::array<std::byte, 64 * 1024> buf;
    bool ok = true;
    for (;;) {
        const auto n = std::fread(buf.data(), 1, buf.size(), f);
        hash.Update(std::span(buf).first(n));
        if (n < buf.size()) { ok = std::ferror(f) == 0; break; }
    }
    std::fclose(f);
    if (ok) out = hash.Finish();
    return ok;
}

std::string SourcePath(const std::vector<std::string>& folders, const Source& source) {
    return Utf8Of(PathOf(folders[source.folder]) / PathOf(source.relative));
}

bool ListSources(const std::vector<std::string>& folders, std::vector<Source>& sources, bool hash) {
    for (std::size_t folder = 0; folder < folders.size(); folder++) {
        std::error_code ec;
        std::vector<std::string> names;
        const auto dir = PathOf(folders[folder]);
        for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
            const auto name = Utf8Of(it->path().filename());
            std::error_code fileEc;
            if (name.ends_with(".tsv") && it->is_regular_file(fileEc)) names.push_back(name);
        }
        if (ec) return false;
        std::sort(names.begin(), names.end());
        for (auto& name : names) {
            Source source;
            source.folder = static_cast<uint32_t>(folder);
            source.relative = std::move(name);
            const auto path = SourcePath(folders, source);
            if (!internal::ReadStamp(path, source.stamp) || (hash && !HashFile(path, source.hash))) return false;
            sources.push_back(std::move(source));
        }
    }
    return true;
}

Buffer Manifest(const std::vector<Source>& sources) {
    Buffer b;
    b.Number(sources.size());
    for (const auto& source : sources) {
        b.Number(source.folder);
        b.Text(source.relative);
        b.Number(source.stamp.size, 8);
        b.Number(static_cast<uint64_t>(source.stamp.mtimeNs), 8);
        b.Number(source.stamp.identityHigh, 8);
        b.Number(source.stamp.identityLow, 8);
        b.Number(source.stamp.platform);
        b.Raw(source.hash);
    }
    return b;
}

void StorePath(Buffer& b, std::string_view path, const std::vector<std::string>& folders) {
    for (std::size_t i = 0; i < folders.size(); i++) {
        std::string prefix = Utf8Of(PathOf(folders[i]));
        while (!prefix.empty() && (prefix.back() == '/' || prefix.back() == '\\')) prefix.pop_back();
        if (path.starts_with(prefix) && path.size() > prefix.size() &&
            (path[prefix.size()] == '/' || path[prefix.size()] == '\\')) {
            b.Signed(static_cast<int>(i));
            b.Text(path.substr(prefix.size() + 1));
            return;
        }
    }
    b.Signed(-1);
    b.Text(path);
}

struct Section {
    uint32_t kind = 0, layer = kNoLayer;
    uint64_t offset = 0, length = 0, count = 0;
    uint32_t width = 1, alignment = 8, first = 0, segments = 0;
};

struct Segment {
    uint32_t section = 0;
    uint64_t start = 0, length = 0;
    uint32_t crc = 0;
};

struct OutputSection {
    Section desc;
    std::vector<std::byte> owned;
    Bytes bytes;
};

struct ImageWriter {
    std::vector<OutputSection> sections;
    Digest contentIdentity{};
    bool ok = true;
    void Add(uint32_t kind, uint32_t layer, Bytes bytes, std::size_t count, uint32_t width) {
        OutputSection s;
        s.desc.kind = kind; s.desc.layer = layer; s.desc.count = count; s.desc.width = width;
        s.bytes = bytes;
        sections.push_back(std::move(s));
    }
    void Add(uint32_t kind, Buffer b) {
        ok = ok && b.ok;
        Add(kind, kNoLayer, {}, b.data.size(), 1);
        sections.back().owned = std::move(b.data);
        sections.back().bytes = sections.back().owned;
    }
};

std::vector<std::byte> Directory(const std::vector<OutputSection>& sections, const std::vector<Segment>& segments) {
    std::vector<std::byte> out((sections.size() + segments.size()) * kDescriptorSize);
    for (std::size_t i = 0; i < sections.size(); i++) {
        const auto& s = sections[i].desc;
        const auto at = i * kDescriptorSize;
        Put(out, at, s.kind, 4); Put(out, at + 4, s.layer, 4);
        Put(out, at + 8, s.offset, 8); Put(out, at + 16, s.length, 8); Put(out, at + 24, s.count, 8);
        Put(out, at + 32, s.width, 4); Put(out, at + 36, s.alignment, 4);
        Put(out, at + 40, s.first, 4); Put(out, at + 44, s.segments, 4);
    }
    for (std::size_t i = 0; i < segments.size(); i++) {
        const auto& s = segments[i];
        const auto at = (sections.size() + i) * kDescriptorSize;
        Put(out, at, s.section, 4); Put(out, at + 8, s.start, 8);
        Put(out, at + 16, s.length, 8); Put(out, at + 24, s.crc, 4);
    }
    return out;
}

bool Publish(const std::filesystem::path& from, const std::filesystem::path& to) {
#ifdef _WIN32
    return MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    std::error_code ec;
    std::filesystem::rename(from, to, ec);
    return !ec;
#endif
}

bool ValidateImage(std::string_view path, const std::vector<std::string>& folders, const Snapshot& expected);

bool WriteImage(ImageWriter& writer, const Snapshot& snap, std::string_view name, const std::vector<std::string>& folders) {
    if constexpr (std::endian::native != std::endian::little) return false;
    if (!writer.ok) return false;
    std::vector<Segment> segments;
    for (std::size_t i = 0; i < writer.sections.size(); i++) {
        auto& s = writer.sections[i];
        s.desc.length = s.bytes.size();
        s.desc.first = static_cast<uint32_t>(segments.size());
        for (std::size_t at = 0; at < s.bytes.size(); at += kSegmentSize) {
            const auto bytes = s.bytes.subspan(at, std::min(kSegmentSize, s.bytes.size() - at));
            segments.push_back({static_cast<uint32_t>(i), at, bytes.size(), Crc32c(bytes)});
        }
        s.desc.segments = static_cast<uint32_t>(segments.size()) - s.desc.first;
    }
    const uint64_t directorySize = (writer.sections.size() + segments.size()) * kDescriptorSize;
    uint64_t end = kHeaderSize + directorySize;
    for (auto& s : writer.sections) {
        end = (end + 7) & ~uint64_t{7};
        s.desc.offset = end;
        end += s.bytes.size();
    }
    const auto directory = Directory(writer.sections, segments);
    std::array<std::byte, kHeaderSize> header{};
    std::memcpy(header.data(), kMagic.data(), kMagic.size());
    Put(header, 8, kFormatVersion, 4); Put(header, 12, kDataVersion, 4);
    Put(header, 16, kHeaderSize, 4); Put(header, 24, end, 8); Put(header, 32, kHeaderSize, 8);
    Put(header, 40, writer.sections.size(), 4); Put(header, 44, segments.size(), 4);
    Put(header, 48, kHeaderSize + writer.sections.size() * kDescriptorSize, 8);
    std::copy(writer.contentIdentity.begin(), writer.contentIdentity.end(), header.begin() + 64);
    const auto builtin = HexDigest(HDTRANS_BUILTIN_ID), rules = HexDigest(HDTRANS_DATA_ID);
    std::copy(builtin.begin(), builtin.end(), header.begin() + 96);
    std::copy(rules.begin(), rules.end(), header.begin() + 128);
    const Dict* layers[] = {&snap.Base(), snap.User(), snap.Names()};
    for (std::size_t i = 0; i < 3; i++) Put(header, 160 + 8 * i, layers[i] ? layers[i]->Len() : 0, 8);
    Put(header, 196, Crc32c(directory), 4); Put(header, 200, 1, 4);
    Put(header, 204, sizeof(void*), 4);
    const auto compiler = HexDigest(HDTRANS_COMPILER_ID);
    std::copy(compiler.begin(), compiler.end(), header.begin() + 208);
    Put(header, 192, Crc32c(header), 4);

    const auto path = PathOf(name);
    std::error_code ec;
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) return false;
    static std::atomic<uint64_t> sequence{0};
    const auto token = std::chrono::steady_clock::now().time_since_epoch().count();
    auto temp = path;
    temp += ".tmp." + std::to_string(token) + "." + std::to_string(sequence.fetch_add(1));
    std::FILE* f = OpenFile(temp, true);
    if (!f) return false;
    bool ok = std::fwrite(header.data(), 1, header.size(), f) == header.size();
    ok = ok && std::fwrite(directory.data(), 1, directory.size(), f) == directory.size();
    uint64_t at = kHeaderSize + directory.size();
    const std::array<std::byte, 8> padding{};
    for (const auto& s : writer.sections) {
        const auto pad = static_cast<std::size_t>(s.desc.offset - at);
        ok = ok && std::fwrite(padding.data(), 1, pad, f) == pad;
        ok = ok && std::fwrite(s.bytes.data(), 1, s.bytes.size(), f) == s.bytes.size();
        at = s.desc.offset + s.bytes.size();
    }
    if (std::fclose(f) != 0) ok = false;
    // Verify every stored byte before publishing the immutable generation.
    if (ok) {
        const auto mapped = internal::MappedFile::Open(Utf8Of(temp));
        ok = mapped && mapped->Bytes().size() == end;
        if (ok) for (const auto& segment : segments) {
            const auto offset = writer.sections[segment.section].desc.offset + segment.start;
            ok = ok && Crc32c(mapped->Bytes().subspan(static_cast<std::size_t>(offset), static_cast<std::size_t>(segment.length))) == segment.crc;
        }
    }
    if (ok) ok = ValidateImage(Utf8Of(temp), folders, snap);
    if (ok) ok = Publish(temp, path);
    if (!ok) std::filesystem::remove(temp, ec);
    return ok;
}

}  // namespace

struct SnapshotStorage {
    static std::shared_ptr<internal::SnapshotFile> Backing(const Snapshot& s) { return s.backing_; }
    static LoadResult Read(const std::vector<std::string>& folders, std::string_view path, bool full);
    static bool RestoreAux(Reader& r, Snapshot& s, std::vector<LoadError>& errors, const std::vector<std::string>& folders);
    static Buffer Aux(const Snapshot& s, const std::vector<LoadError>& errors, const std::vector<std::string>& folders);
    static bool Write(const Snapshot& s, const std::vector<LoadError>& errors, const std::vector<Source>& sources,
                      std::string_view path, const std::vector<std::string>& folders);
};

Buffer SnapshotStorage::Aux(const Snapshot& s, const std::vector<LoadError>& errors, const std::vector<std::string>& folders) {
    Buffer b;
    b.Number(s.hasDictionary_ ? 1 : 0);
    b.Number(errors.size());
    for (const auto& e : errors) {
        StorePath(b, e.file, folders); b.Number(static_cast<uint8_t>(e.kind)); b.Signed(e.line);
        b.Number(static_cast<uint8_t>(e.tsv)); b.Text(e.message);
    }
    b.Number(s.warnings_.size());
    for (const auto& warning : s.warnings_) StorePath(b, warning, folders);
    b.Number(s.places_.size());
    for (const auto& [key, sp] : s.places_) {
        b.Text(key); b.Text(sp.ko); b.Text(sp.person); b.Number(static_cast<uint8_t>(sp.mode));
    }
    b.Number(s.plain_.size());
    for (const auto& f : s.plain_) { b.Signed(f.folder); StorePath(b, f.name, folders); b.Text(f.text); }
    std::unordered_map<const Codes*, uint32_t> codeIds;
    for (std::size_t i = 0; i < s.codes_.Size(); i++) codeIds.emplace(s.codes_.At(static_cast<int32_t>(i)), static_cast<uint32_t>(i));
    for (const auto* u : {s.rawPlain_.get(), s.textPlain_.get(), s.rawExtraPlain_.get(), s.textExtraPlain_.get()}) {
        b.Number(u->Entries().size());
        for (const auto& e : u->Entries()) {
            b.Text(e.key); b.Text(e.target); b.Text(e.negative);
            b.Number(e.codes ? codeIds.at(e.codes) : std::numeric_limits<uint32_t>::max());
            b.Signed(e.prio); b.Signed(e.file); b.Signed(e.line); b.Signed(e.order);
            const bool flags[] = {e.name, e.nameLayer, e.kana, e.force, e.hidden, e.honorific, e.end, e.polite, e.stative, e.euImperative};
            uint32_t bits = 0;
            for (std::size_t i = 0; i < std::size(flags); i++) if (flags[i]) bits |= uint32_t{1} << i;
            b.Number(bits); b.Number(static_cast<unsigned char>(e.finalClass), 1); b.Number(e.registerLevel, 1);
        }
    }
    b.Number(s.senses_.size());
    for (const auto& [key, list] : s.senses_) {
        b.Text(key); b.Number(list.size());
        for (const auto& sense : list) {
            b.Text(sense.key); b.Text(sense.target); b.Number(static_cast<unsigned char>(sense.classDigit), 1);
            b.Number(static_cast<uint8_t>(sense.side), 1); b.Number(sense.adjective ? 1 : 0, 1); b.Signed(sense.row);
            b.Number(sense.triggers.size());
            for (const auto& trigger : sense.triggers) b.Text(trigger);
        }
    }
    auto families = s.families_.GeneratedFamilies();
    std::sort(families.begin(), families.end(), [](const auto& a, const auto& c) { return a.first < c.first; });
    b.Number(families.size());
    for (const auto& [id, rows] : families) { b.Text(id); for (const auto& row : rows) b.Text(row); }
    return b;
}

bool SnapshotStorage::Write(const Snapshot& s, const std::vector<LoadError>& errors,
                            const std::vector<Source>& sources, std::string_view path, const std::vector<std::string>& folders) {
    ImageWriter writer;
    writer.Add(kManifest, Manifest(sources));
    Buffer identity;
    for (const auto& source : sources) { identity.Number(source.folder); identity.Text(source.relative); identity.Raw(source.hash); }
    writer.contentIdentity = internal::Hash256(identity.data);
    Buffer codes;
    codes.Number(s.codes_.Size());
    std::unordered_map<const Codes*, uint32_t> codeIds;
    for (std::size_t i = 0; i < s.codes_.Size(); i++) {
        const auto* code = s.codes_.At(static_cast<int32_t>(i));
        codeIds.emplace(code, static_cast<uint32_t>(i));
        for (const auto text : {std::string_view(code->partText), code->sem.Text(), code->gram.Text(), code->dict.Text()}) codes.Text(text);
    }
    writer.Add(kCodes, std::move(codes));
    writer.Add(kAux, Aux(s, errors, folders));
    const Dict* layers[] = {&s.base_, &s.user_, &s.names_};
    for (uint32_t layer = 0; layer < 3; layer++) {
        const auto view = layers[layer]->View();
        auto add = [&]<class T>(uint32_t kind, std::span<T> values) {
            writer.Add(kind, layer, std::as_bytes(values), values.size(), static_cast<uint32_t>(sizeof(T)));
        };
        add(kTree, view.keys.tree); add(kZeros, view.keys.zeros); add(kLabel, view.keys.label);
        add(kTerminal, view.keys.terminal); add(kRank, view.keys.rank); add(kStarts, view.starts);
        Buffer refs;
        for (const auto* code : view.codes) refs.Number(codeIds.at(code));
        writer.Add(kCodeRefs, layer, {}, view.codes.size(), 4);
        writer.sections.back().owned = std::move(refs.data);
        writer.sections.back().bytes = writer.sections.back().owned;
        writer.Add(kBlob, layer, ByteView(view.blob), view.blob.size(), 1);
    }
    return WriteImage(writer, s, path, folders);
}


namespace {

struct BadResource { std::string path; FileStamp stamp; };
std::mutex& BadMutex() { static std::mutex mutex; return mutex; }
std::vector<BadResource>& BadResources() { static std::vector<BadResource> list; return list; }

bool IsBad(std::string_view path, const FileStamp& stamp) {
    std::lock_guard lock(BadMutex());
    for (const auto& entry : BadResources()) if (entry.path == path && entry.stamp == stamp) return true;
    return false;
}

std::string RestorePath(Reader& r, const std::vector<std::string>& folders) {
    const int folder = r.Signed();
    const auto name = r.Text();
    if (!SafePath(name)) { r.ok = false; return {}; }
    if (folder == -1) return std::string(name);
    if (folder < 0 || static_cast<std::size_t>(folder) >= folders.size()) { r.ok = false; return {}; }
    return Utf8Of(PathOf(folders[static_cast<std::size_t>(folder)]) / PathOf(name));
}

bool ReadManifest(Bytes bytes, std::vector<Source>& sources, std::size_t folders) {
    Reader r{bytes};
    const auto count = r.Count(76);
    sources.reserve(count);
    for (std::size_t i = 0; i < count && r.ok; i++) {
        Source source;
        source.folder = static_cast<uint32_t>(r.Number());
        source.relative = r.Text();
        if (!SafePath(source.relative)) return false;
        const auto path = PathOf(source.relative);
        if (source.folder >= folders || source.relative.empty() || path.has_parent_path() ||
            path.is_absolute() || source.relative == "." || source.relative == ".." || !source.relative.ends_with(".tsv")) r.ok = false;
        source.stamp.size = r.Number(8);
        source.stamp.mtimeNs = static_cast<int64_t>(r.Number(8));
        source.stamp.identityHigh = r.Number(8); source.stamp.identityLow = r.Number(8);
        source.stamp.platform = static_cast<uint32_t>(r.Number());
        const auto digest = r.Take(source.hash.size());
        if (r.ok) std::copy(digest.begin(), digest.end(), source.hash.begin());
        sources.push_back(std::move(source));
    }
    return r.Done();
}

Digest ContentIdentity(const std::vector<Source>& sources) {
    Buffer b;
    for (const auto& source : sources) { b.Number(source.folder); b.Text(source.relative); b.Raw(source.hash); }
    return internal::Hash256(b.data);
}

}  // namespace

namespace internal {

class SnapshotFile {
public:
    struct LayerGuard { SnapshotFile* file = nullptr; uint32_t layer = 0; std::atomic<uint8_t> ready{0}; };
    std::shared_ptr<const MappedFile> mapped;
    std::string path;
    FileStamp stamp;
    std::vector<std::string> folders;
    std::vector<Section> sections;
    std::vector<Segment> segments;
    std::unique_ptr<std::atomic<uint8_t>[]> checked;
    std::array<LayerGuard, 3> guards;
    std::array<std::vector<const Codes*>, 3> codeRefs;
    std::array<std::size_t, 3> keys{};
    std::vector<Source> sources;
    std::atomic<bool> bad{false};
    std::once_flag fallbackOnce;
    std::shared_ptr<const Snapshot> fallback;

    void Fail() {
        if (bad.exchange(true, std::memory_order_relaxed)) return;
        std::lock_guard lock(BadMutex());
        auto& list = BadResources();
        if (list.size() == 128) list.erase(list.begin());
        list.push_back({path, stamp});
    }
    const Section* Find(uint32_t kind, uint32_t layer = kNoLayer) const {
        for (const auto& s : sections) if (s.kind == kind && s.layer == layer) return &s;
        return nullptr;
    }
    Bytes Raw(const Section& s) const {
        return mapped->Bytes().subspan(static_cast<std::size_t>(s.offset), static_cast<std::size_t>(s.length));
    }
    bool EnsureSegment(std::size_t i) {
        if (bad.load(std::memory_order_relaxed)) return false;
        if (checked[i].load(std::memory_order_acquire) == 1) return true;
        const auto& segment = segments[i];
        const auto& section = sections[segment.section];
        if (Crc32c(Raw(section).subspan(static_cast<std::size_t>(segment.start), static_cast<std::size_t>(segment.length))) != segment.crc) {
            Fail(); return false;
        }
        checked[i].store(1, std::memory_order_release);
        return !bad.load(std::memory_order_relaxed);
    }
    bool Ensure(const Section& s) {
        for (std::size_t i = s.first; i < std::size_t{s.first} + s.segments; i++) if (!EnsureSegment(i)) return false;
        return !bad.load(std::memory_order_relaxed);
    }
    bool Range(const void* pointer, std::size_t length) {
        if (bad.load(std::memory_order_relaxed)) return false;
        const auto base = reinterpret_cast<uintptr_t>(mapped->Bytes().data());
        const auto address = reinterpret_cast<uintptr_t>(pointer);
        if (address < base || address - base > mapped->Bytes().size()) { Fail(); return false; }
        const auto at = static_cast<std::size_t>(address - base);
        if (length > mapped->Bytes().size() - at) { Fail(); return false; }
        for (const auto& s : sections) {
            if (at < s.offset || at - s.offset > s.length || length > s.length - (at - s.offset)) continue;
            if (length == 0) return true;
            const auto lo = (at - static_cast<std::size_t>(s.offset)) / kSegmentSize;
            const auto hi = (at - static_cast<std::size_t>(s.offset) + length - 1) / kSegmentSize;
            if (hi >= s.segments) { Fail(); return false; }
            for (std::size_t i = lo; i <= hi; i++) if (!EnsureSegment(s.first + i)) return false;
            return true;
        }
        Fail(); return false;
    }
    bool TrieReady(uint32_t layer) {
        auto& guard = guards[layer];
        if (bad.load(std::memory_order_relaxed)) return false;
        if (guard.ready.load(std::memory_order_acquire)) return true;
        for (uint32_t kind = kTree; kind <= kRank; kind++) if (!Ensure(*Find(kind, layer))) return false;
        guard.ready.store(1, std::memory_order_release);
        return true;
    }
    static bool Check(const void* context, const void* pointer, std::size_t size) {
        auto& guard = *const_cast<LayerGuard*>(static_cast<const LayerGuard*>(context));
        return pointer ? guard.file->Range(pointer, size) : guard.file->TrieReady(guard.layer);
    }
    static void MarkBad(const void* context) {
        static_cast<const LayerGuard*>(context)->file->Fail();
    }
    std::shared_ptr<const Snapshot> Fallback() {
        std::call_once(fallbackOnce, [&] { fallback = LoadFoldersTsv(folders).snapshot; });
        return fallback;
    }
    template<class T> std::span<const T> Array(uint32_t kind, uint32_t layer) const {
        const auto* section = Find(kind, layer);
        const auto bytes = Raw(*section);
        return {reinterpret_cast<const T*>(bytes.data()), static_cast<std::size_t>(section->count)};
    }
    bool Open(std::string_view name, const std::vector<std::string>& dirs) {
        if constexpr (std::endian::native != std::endian::little) return false;
        path = name; folders = dirs;
        if (!ReadStamp(name, stamp) || IsBad(name, stamp)) return false;
        mapped = MappedFile::Open(name);
        FileStamp after;
        if (!mapped || !ReadStamp(name, after) || stamp != after) return false;
        const auto bytes = mapped->Bytes();
        if (bytes.size() < kHeaderSize || std::memcmp(bytes.data(), kMagic.data(), 8) != 0 ||
            Get(bytes, 8, 4) != kFormatVersion || Get(bytes, 12, 4) != kDataVersion ||
            Get(bytes, 16, 4) != kHeaderSize || Get(bytes, 20, 4) != 0 ||
            Get(bytes, 24, 8) != bytes.size() || Get(bytes, 32, 8) != kHeaderSize || Get(bytes, 200, 4) != 1) return false;
        auto header = std::array<std::byte, kHeaderSize>{};
        std::copy_n(bytes.begin(), kHeaderSize, header.begin());
        Put(header, 192, 0, 4);
        if (Crc32c(header) != Get(bytes, 192, 4)) return false;
        const auto builtin = HexDigest(HDTRANS_BUILTIN_ID), rules = HexDigest(HDTRANS_DATA_ID);
        const auto compiler = HexDigest(HDTRANS_COMPILER_ID);
        if (!std::equal(builtin.begin(), builtin.end(), bytes.begin() + 96) ||
            !std::equal(rules.begin(), rules.end(), bytes.begin() + 128) ||
            Get(bytes, 204, 4) != sizeof(void*) || !std::equal(compiler.begin(), compiler.end(), bytes.begin() + 208)) return false;
        const auto sectionCount = Get(bytes, 40, 4), segmentCount = Get(bytes, 44, 4);
        if (sectionCount != 27 || segmentCount > (bytes.size() - kHeaderSize) / kDescriptorSize ||
            sectionCount > (bytes.size() - kHeaderSize) / kDescriptorSize - segmentCount) return false;
        const auto directorySize = static_cast<std::size_t>((sectionCount + segmentCount) * kDescriptorSize);
        if (Get(bytes, 48, 8) != kHeaderSize + sectionCount * kDescriptorSize ||
            Crc32c(bytes.subspan(kHeaderSize, directorySize)) != Get(bytes, 196, 4)) return false;
        uint64_t end = kHeaderSize + directorySize;
        uint64_t nextSegment = 0;
        for (std::size_t i = 0; i < sectionCount; i++) {
            const auto at = kHeaderSize + i * kDescriptorSize;
            Section s;
            s.kind = static_cast<uint32_t>(Get(bytes, at, 4)); s.layer = static_cast<uint32_t>(Get(bytes, at + 4, 4));
            s.offset = Get(bytes, at + 8, 8); s.length = Get(bytes, at + 16, 8); s.count = Get(bytes, at + 24, 8);
            s.width = static_cast<uint32_t>(Get(bytes, at + 32, 4)); s.alignment = static_cast<uint32_t>(Get(bytes, at + 36, 4));
            s.first = static_cast<uint32_t>(Get(bytes, at + 40, 4)); s.segments = static_cast<uint32_t>(Get(bytes, at + 44, 4));
            const bool metadata = s.kind >= kManifest && s.kind <= kAux && s.layer == kNoLayer;
            const bool layer = s.kind >= kTree && s.kind <= kBlob && s.layer < 3;
            const uint32_t width = s.kind == kTree || s.kind == kTerminal ? 8 : s.kind == kLabel ? 2 :
                                   s.kind == kZeros || s.kind == kRank || s.kind == kStarts || s.kind == kCodeRefs ? 4 : 1;
            if ((!metadata && !layer) || Find(s.kind, s.layer) || s.width != width || s.alignment != 8 ||
                s.offset % 8 != 0 || s.offset < end || s.offset > bytes.size() || s.length > bytes.size() - s.offset ||
                s.count > std::numeric_limits<std::size_t>::max() / width || s.count * width != s.length ||
                s.first != nextSegment || s.segments > segmentCount - nextSegment ||
                s.segments != (s.length + kSegmentSize - 1) / kSegmentSize) return false;
            for (std::size_t reserved = at + 48; reserved < at + kDescriptorSize; reserved++) if (bytes[reserved] != std::byte{0}) return false;
            nextSegment += s.segments;
            end = s.offset + s.length;
            sections.push_back(s);
        }
        if (nextSegment != segmentCount || end != bytes.size()) return false;
        for (std::size_t i = 0; i < segmentCount; i++) {
            const auto at = kHeaderSize + (static_cast<std::size_t>(sectionCount) + i) * kDescriptorSize;
            Segment segment;
            segment.section = static_cast<uint32_t>(Get(bytes, at, 4)); segment.start = Get(bytes, at + 8, 8);
            segment.length = Get(bytes, at + 16, 8); segment.crc = static_cast<uint32_t>(Get(bytes, at + 24, 4));
            if (segment.section >= sections.size()) return false;
            const auto& owner = sections[segment.section];
            if (i < owner.first || i >= std::size_t{owner.first} + owner.segments ||
                segment.start != (i - owner.first) * kSegmentSize || segment.start >= owner.length ||
                segment.length != std::min<uint64_t>(kSegmentSize, owner.length - segment.start)) return false;
            if (Get(bytes, at + 4, 4) != 0) return false;
            for (std::size_t reserved = at + 28; reserved < at + kDescriptorSize; reserved++) if (bytes[reserved] != std::byte{0}) return false;
            segments.push_back(segment);
        }
        checked = std::make_unique<std::atomic<uint8_t>[]>(segments.size());
        for (std::size_t i = 0; i < segments.size(); i++) checked[i].store(0, std::memory_order_relaxed);
        for (uint32_t layer = 0; layer < 3; layer++) {
            keys[layer] = static_cast<std::size_t>(Get(bytes, 160 + 8 * layer, 8));
            if (Get(bytes, 160 + 8 * layer, 8) > static_cast<uint64_t>(std::numeric_limits<int32_t>::max())) return false;
            guards[layer].file = this; guards[layer].layer = layer;
            for (uint32_t kind = kTree; kind <= kBlob; kind++) if (!Find(kind, layer)) return false;
        }
        const auto* manifest = Find(kManifest);
        if (!manifest || !Find(kCodes) || !Find(kAux) || !Ensure(*manifest) || !ReadManifest(Raw(*manifest), sources, folders.size())) return false;
        const auto identity = ContentIdentity(sources);
        return std::equal(identity.begin(), identity.end(), bytes.begin() + 64);
    }
};

}  // namespace internal

bool SnapshotStorage::RestoreAux(Reader& r, Snapshot& s, std::vector<LoadError>& errors, const std::vector<std::string>& folders) {
    const auto hasDictionary = r.Number();
    if (hasDictionary > 1) return false;
    s.hasDictionary_ = hasDictionary != 0;
    auto count = r.Count(24);
    for (std::size_t i = 0; i < count && r.ok; i++) {
        LoadError e;
        e.file = RestorePath(r, folders);
        const auto kind = r.Number(); e.line = r.Signed(); const auto tsv = r.Number(); e.message = r.Text();
        if (kind > static_cast<unsigned>(LoadErrorKind::Plain) || tsv > static_cast<unsigned>(TsvErrorKind::EmptyKeyOrId)) return false;
        e.kind = static_cast<LoadErrorKind>(kind); e.tsv = static_cast<TsvErrorKind>(tsv);
        errors.push_back(std::move(e));
    }
    count = r.Count(8);
    for (std::size_t i = 0; i < count && r.ok; i++) s.warnings_.push_back(RestorePath(r, folders));
    count = r.Count(16);
    for (std::size_t i = 0; i < count && r.ok; i++) {
        const std::string key(r.Text());
        Snapshot::Respelling sp;
        sp.ko = r.Text(); sp.person = r.Text(); const auto mode = r.Number();
        if (mode > static_cast<unsigned>(Snapshot::Respelling::Mode::AllNouns)) return false;
        sp.mode = static_cast<Snapshot::Respelling::Mode>(mode);
        if (!s.places_.emplace(key, std::move(sp)).second) return false;
    }
    count = r.Count(16);
    for (std::size_t i = 0; i < count && r.ok; i++) {
        File file;
        file.folder = r.Signed(); file.name = RestorePath(r, folders); file.text = r.Text();
        if (file.folder < 0 || static_cast<std::size_t>(file.folder) >= folders.size()) return false;
        s.plain_.push_back(std::move(file));
    }
    for (auto* output : {&s.rawPlain_, &s.textPlain_, &s.rawExtraPlain_, &s.textExtraPlain_}) {
        count = r.Count(38);
        std::vector<UserEntry> entries;
        entries.reserve(count);
        for (std::size_t i = 0; i < count && r.ok; i++) {
            UserEntry e;
            e.key = r.Text(); e.target = r.Text(); e.negative = r.Text();
            const auto code = r.Number();
            if (code >= s.codes_.Size()) return false;
            e.codes = s.codes_.At(static_cast<int32_t>(code));
            e.prio = r.Signed(); e.file = r.Signed(); e.line = r.Signed(); e.order = r.Signed();
            const auto flags = r.Number();
            if (flags >= (1 << 10)) return false;
            e.name = (flags & 1) != 0; e.nameLayer = (flags & 2) != 0; e.kana = (flags & 4) != 0;
            e.force = (flags & 8) != 0; e.hidden = (flags & 16) != 0; e.honorific = (flags & 32) != 0;
            e.end = (flags & 64) != 0; e.polite = (flags & 128) != 0; e.stative = (flags & 256) != 0;
            e.euImperative = (flags & 512) != 0;
            e.finalClass = static_cast<char>(r.Number(1)); e.registerLevel = static_cast<uint8_t>(r.Number(1));
            entries.push_back(std::move(e));
        }
        if (!r.ok) return false;
        *output = std::make_unique<const UserText>(std::move(entries));
    }
    count = r.Count(8);
    for (std::size_t i = 0; i < count && r.ok; i++) {
        std::string key(r.Text());
        const auto senses = r.Count(19);
        std::vector<userdict::Sense> list;
        list.reserve(senses);
        for (std::size_t j = 0; j < senses && r.ok; j++) {
            userdict::Sense sense;
            sense.key = r.Text(); sense.target = r.Text(); sense.classDigit = static_cast<char>(r.Number(1));
            const auto side = r.Number(1), adjective = r.Number(1);
            if (side > static_cast<unsigned>(userdict::Side::Sentence) || adjective > 1) return false;
            sense.side = static_cast<userdict::Side>(side); sense.adjective = adjective != 0; sense.row = r.Signed();
            const auto triggers = r.Count(4);
            for (std::size_t k = 0; k < triggers && r.ok; k++) sense.triggers.emplace_back(r.Text());
            list.push_back(std::move(sense));
        }
        if (!s.senses_.emplace(std::move(key), std::move(list)).second) return false;
    }
    count = r.Count(52);
    for (std::size_t i = 0; i < count && r.ok; i++) {
        const auto id = r.Text();
        userdict::FamilyRows rows;
        for (auto& row : rows) row = r.Text();
        if (!r.ok || id.empty()) return false;
        std::string restored;
        if (!s.families_.Generate(id[0], rows, restored).empty() || restored != id) return false;
    }
    return r.Done();
}

LoadResult SnapshotStorage::Read(const std::vector<std::string>& folders, std::string_view path, bool full) {
    auto file = std::make_shared<internal::SnapshotFile>();
    if (!file->Open(path, folders)) return {};
    auto snap = std::shared_ptr<Snapshot>(new Snapshot());
    snap->backing_ = file;
    const auto* codeSection = file->Find(kCodes);
    if (!file->Ensure(*codeSection)) return {};
    Reader codes{file->Raw(*codeSection)};
    const auto count = codes.Count(16);
    for (std::size_t i = 0; i < count && codes.ok; i++) {
        const auto part = codes.Text(), sem = codes.Text(), gram = codes.Text(), dict = codes.Text();
        if (!codes.ok || snap->codes_.Intern(part, sem, gram, dict) != static_cast<int32_t>(i)) return {};
    }
    if (!codes.Done() || count > static_cast<std::size_t>(std::numeric_limits<int32_t>::max())) return {};
    for (std::size_t i = 0; i < count; i++) { snap->codes_.SetWithXXX(static_cast<int32_t>(i)); snap->codes_.SetAsPlain(static_cast<int32_t>(i)); }
    if (snap->codes_.Size() != count) return {};
    Dict* layers[] = {&snap->base_, &snap->user_, &snap->names_};
    for (uint32_t layer = 0; layer < 3; layer++) {
        const auto* refs = file->Find(kCodeRefs, layer);
        if (!file->Ensure(*refs)) return {};
        auto& pointers = file->codeRefs[layer];
        if (refs->count > count) return {};
        pointers.reserve(static_cast<std::size_t>(refs->count));
        const auto bytes = file->Raw(*refs);
        for (std::size_t i = 0; i < refs->count; i++) {
            const auto code = Get(bytes, 4 * i, 4);
            if (code >= count) return {};
            pointers.push_back(snap->codes_.At(static_cast<int32_t>(code)));
        }
        Dict::ViewData data;
        data.keys = {file->Array<uint64_t>(kTree, layer), file->Array<uint32_t>(kZeros, layer), file->Array<uint16_t>(kLabel, layer),
                     file->Array<uint64_t>(kTerminal, layer), file->Array<uint32_t>(kRank, layer), file->keys[layer]};
        const auto blob = file->Raw(*file->Find(kBlob, layer));
        data.blob = {reinterpret_cast<const char*>(blob.data()), blob.size()};
        data.starts = file->Array<uint32_t>(kStarts, layer); data.codes = pointers;
        data.guard = {&file->guards[layer], internal::SnapshotFile::Check, internal::SnapshotFile::MarkBad};
        data.keys.guard = data.guard;
        *layers[layer] = Dict::FromView(data);
    }
    const auto* auxSection = file->Find(kAux);
    if (!file->Ensure(*auxSection)) return {};
    Reader aux{file->Raw(*auxSection)};
    LoadResult loaded;
    if (!RestoreAux(aux, *snap, loaded.errors, folders)) return {};
    if (full) {
        for (const auto& section : file->sections) if (!file->Ensure(section)) return {};
        for (const auto* layer : layers) if (!layer->Validate()) { file->Fail(); return {}; }
    }
    if (file->bad.load(std::memory_order_relaxed)) return {};
    loaded.snapshot = std::move(snap);
    return loaded;
}

namespace {

bool ValidateImage(std::string_view path, const std::vector<std::string>& folders, const Snapshot& expected) {
    const auto loaded = SnapshotStorage::Read(folders, path, true);
    if (!loaded.snapshot || loaded.snapshot->Warnings() != expected.Warnings()) return false;
    const Dict* wanted[] = {&expected.Base(), expected.User(), expected.Names()};
    const Dict* actual[] = {&loaded.snapshot->Base(), loaded.snapshot->User(), loaded.snapshot->Names()};
    for (std::size_t layer = 0; layer < 3; layer++) {
        if (!wanted[layer] || !actual[layer]) {
            if (wanted[layer] != actual[layer]) return false;
            continue;
        }
        if (wanted[layer]->Len() != actual[layer]->Len()) return false;
        std::size_t keys = 0;
        bool equal = true;
        wanted[layer]->Each([&](std::string_view key, const std::vector<Record>& records) {
            keys++;
            const auto got = actual[layer]->Records(key);
            if (got.size() != records.size() || wanted[layer]->Find(key) != actual[layer]->Find(key)) {
                equal = false;
                return;
            }
            for (std::size_t i = 0; i < records.size(); i++) {
                const auto& a = records[i];
                const auto& b = got[i];
                if (a.target != b.target || a.rule != b.rule || a.id != b.id || !a.codes || !b.codes ||
                    a.codes->partText != b.codes->partText || a.codes->sem.Text() != b.codes->sem.Text() ||
                    a.codes->gram.Text() != b.codes->gram.Text() || a.codes->dict.Text() != b.codes->dict.Text()) equal = false;
            }
        });
        if (!equal || keys != wanted[layer]->Len() || !loaded.snapshot->Healthy()) return false;
    }
    return true;
}

}  // namespace

bool Snapshot::Healthy() const { return !backing_ || !backing_->bad.load(std::memory_order_relaxed); }
std::shared_ptr<const Snapshot> Snapshot::Fallback() const { return Healthy() ? nullptr : backing_->Fallback(); }
bool Snapshot::Mapped() const { return backing_ != nullptr; }

namespace internal {

std::string SnapshotPath(const std::vector<std::string>& folders, std::string_view cacheDir) {
    const auto overrideDir = EnvironmentPath("HDTRANS_CACHE_DIR");
    if (overrideDir && overrideDir->empty()) return {};
    const auto dir = !cacheDir.empty() ? std::string(cacheDir) : overrideDir ? *overrideDir : DefaultCacheDirectory();
    if (folders.empty() || dir.empty() || !SafePath(dir)) return {};
    Buffer key;
    key.Number(kDataVersion);
    for (const auto& folder : folders) {
        if (!SafePath(folder)) return {};
        std::error_code ec;
        auto absolute = std::filesystem::absolute(PathOf(folder), ec).lexically_normal();
        if (ec) return {};
        if (!absolute.has_filename() && absolute != absolute.root_path()) absolute = absolute.parent_path();
        key.Text(Utf8Of(absolute));
    }
    const auto hash = Hash256(key.data);
    return Utf8Of(PathOf(dir) / (Hex(hash) + ".hdsnap"));
}

}  // namespace internal

bool CompileSnapshot(const std::vector<std::string>& folders, std::string_view outPath) {
    if (!SafePath(outPath)) return false;
    for (const auto& folder : folders) if (!SafePath(folder)) return false;
    std::vector<Source> sources;
    if (!ListSources(folders, sources, true)) return false;
    const auto loaded = LoadFoldersTsv(folders);
    if (!loaded.snapshot) return false;
    std::vector<Source> after;
    if (!ListSources(folders, after, false) || after.size() != sources.size()) return false;
    for (std::size_t i = 0; i < sources.size(); i++) {
        if (sources[i].folder != after[i].folder || sources[i].relative != after[i].relative || sources[i].stamp != after[i].stamp) return false;
    }
    return SnapshotStorage::Write(*loaded.snapshot, loaded.errors, sources, outPath, folders);
}

namespace {

bool CheckSources(const std::vector<std::string>& folders, const std::vector<Source>& recorded,
                  std::vector<Source>& current, bool strict, bool& refresh) {
    if (recorded.size() != current.size()) return false;
    for (std::size_t i = 0; i < current.size(); i++) {
        auto& source = current[i];
        const auto& previous = recorded[i];
        if (source.folder != previous.folder || source.relative != previous.relative) return false;
        if (strict || source.stamp != previous.stamp) {
            if (!HashFile(SourcePath(folders, source), source.hash) || source.hash != previous.hash) return false;
            FileStamp after;
            if (!internal::ReadStamp(SourcePath(folders, source), after) || after != source.stamp) return false;
            refresh = refresh || source.stamp != previous.stamp;
        } else source.hash = previous.hash;
    }
    return true;
}

std::vector<Source> CachedManifest(const internal::SnapshotFile& file) {
    const auto index = internal::MappedFile::Open(file.path + ".meta");
    if (!index) return file.sources;
    const auto bytes = index->Bytes();
    if (bytes.size() < 44 || std::memcmp(bytes.data(), "HDMETA\r\n", 8) != 0 ||
        Crc32c(bytes.first(bytes.size() - 4)) != Get(bytes, bytes.size() - 4, 4)) return file.sources;
    const auto identity = ContentIdentity(file.sources);
    if (!std::equal(identity.begin(), identity.end(), bytes.begin() + 8)) return file.sources;
    std::vector<Source> sources;
    if (!ReadManifest(bytes.subspan(40, bytes.size() - 44), sources, file.folders.size()) || ContentIdentity(sources) != identity) return file.sources;
    return sources;
}

void RefreshManifest(const internal::SnapshotFile& file, const std::vector<Source>& sources) {
    Buffer b;
    b.Raw(ByteView("HDMETA\r\n")); b.Raw(ContentIdentity(sources)); b.Raw(Manifest(sources).data);
    b.Number(Crc32c(b.data));
    const auto path = PathOf(file.path + ".meta");
    auto temp = path;
    static std::atomic<uint64_t> sequence{0};
    temp += ".tmp." + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
            "." + std::to_string(sequence.fetch_add(1));
    std::FILE* f = OpenFile(temp, true);
    if (!f) return;
    bool ok = std::fwrite(b.data.data(), 1, b.data.size(), f) == b.data.size();
    if (std::fclose(f) != 0) ok = false;
    if (ok) ok = Publish(temp, path);
    if (!ok) { std::error_code ec; std::filesystem::remove(temp, ec); }
}

}  // namespace

LoadResult LoadFolders(const std::vector<std::string>& folders, std::string_view cacheDir, bool verify) {
    for (const auto& folder : folders) if (!SafePath(folder)) return {nullptr, {{folder, LoadErrorKind::Folder, 0, TsvErrorKind::Read, {}}}};
    const char* mode = std::getenv("HDTRANS_SNAPSHOT");
    if (mode && std::string_view(mode) == "off") return LoadFoldersTsv(folders);
    const char* strict = std::getenv("HDTRANS_VERIFY_DICTS");
    verify = verify || (strict && std::string_view(strict) == "1");
    const auto path = internal::SnapshotPath(folders, cacheDir);
    if (path.empty()) return LoadFoldersTsv(folders);
    std::vector<Source> sources;
    if (!ListSources(folders, sources, false)) return LoadFoldersTsv(folders);
    if (auto cached = SnapshotStorage::Read(folders, path, verify); cached.snapshot) {
        auto& file = *SnapshotStorage::Backing(*cached.snapshot);
        bool refresh = false;
        if (CheckSources(folders, CachedManifest(file), sources, verify, refresh)) {
            if (refresh) RefreshManifest(file, sources);
            return cached;
        }
    }
    sources.clear();
    bool write = ListSources(folders, sources, true);
    auto loaded = LoadFoldersTsv(folders);
    if (loaded.snapshot && write) {
        std::vector<Source> after;
        write = ListSources(folders, after, false) && after.size() == sources.size();
        for (std::size_t i = 0; write && i < sources.size(); i++) {
            write = sources[i].folder == after[i].folder && sources[i].relative == after[i].relative && sources[i].stamp == after[i].stamp;
        }
        if (write) SnapshotStorage::Write(*loaded.snapshot, loaded.errors, sources, path, folders);
    }
    return loaded;
}

}  // namespace hdTrans::dict
