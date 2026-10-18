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
#include <system_error>
#include <unordered_map>

#ifdef _WIN32
#include <windows.h>
#endif

#include "hdTrans/dict/integrity.h"
#include "hdTrans/dict/mapped_file.h"

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

std::filesystem::path PathOf(std::string_view s) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size()));
}

std::string Utf8Of(const std::filesystem::path& p) {
    const auto s = p.u8string();
    return {reinterpret_cast<const char*>(s.data()), s.size()};
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
        const std::string prefix = Utf8Of(PathOf(folders[i]));
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

bool WriteImage(ImageWriter& writer, const Snapshot& snap, std::string_view name) {
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
    if (ok) ok = Publish(temp, path);
    if (!ok) std::filesystem::remove(temp, ec);
    return ok;
}

}  // namespace

struct SnapshotStorage {
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
            const bool flags[] = {e.name, e.nameLayer, e.kana, e.force, e.hidden, e.honorific, e.end, e.polite, e.stative};
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
            writer.Add(kind, layer, std::as_bytes(values), values.size(), sizeof(T));
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
    return WriteImage(writer, s, path);
}

namespace internal {

std::string SnapshotPath(const std::vector<std::string>& folders, std::string_view cacheDir) {
    if (folders.empty()) return {};
    Buffer key;
    for (const auto& folder : folders) key.Text(folder);
    const auto hash = Hash256(key.data);
    const auto dir = cacheDir.empty() ? PathOf(folders.front()) / ".hdsnap" : PathOf(cacheDir);
    return Utf8Of(dir / (Hex(hash) + ".hdsnap"));
}

}  // namespace internal

bool CompileSnapshot(const std::vector<std::string>& folders, std::string_view outPath) {
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

LoadResult LoadFolders(const std::vector<std::string>& folders, std::string_view, bool) {
    return LoadFoldersTsv(folders);
}

}  // namespace hdTrans::dict
