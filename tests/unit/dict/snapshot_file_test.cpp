#include "hdTrans/dict/snapshot_file.h"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <vector>
#include <string>
#include <tuple>
#include <system_error>

#include "hdTrans/dict/mapped_file.h"
#include "hdTrans/dict/integrity.h"
#include "hdtest.h"

namespace {

std::string FixtureFolder() {
    const auto path = std::filesystem::path(__FILE__).parent_path() / "testdata" / "folder";
    const auto text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

struct TempFolder {
    std::filesystem::path path;
    TempFolder() {
        std::error_code ec;
        path = std::filesystem::temp_directory_path(ec) /
               ("hdtrans-snapshot-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(path, ec);
    }
    ~TempFolder() { std::error_code ec; std::filesystem::remove_all(path, ec); }
    std::string File(std::string_view name) const {
        const auto text = (path / std::filesystem::path(name)).u8string();
        return {reinterpret_cast<const char*>(text.data()), text.size()};
    }
};

}  // namespace

HD_TEST(SnapshotFileWriter) {
    TempFolder temp;
    const auto file = temp.File("dict.hdsnap");
    HD_REQUIRE(hdTrans::dict::CompileSnapshot({FixtureFolder()}, file));
    const auto mapped = hdTrans::dict::internal::MappedFile::Open(file);
    HD_REQUIRE(mapped != nullptr);
    const auto bytes = mapped->Bytes();
    HD_REQUIRE(bytes.size() > 256);
    HD_CHECK(std::memcmp(bytes.data(), "HDSNAP\r\n", 8) == 0);
    HD_CHECK(bytes[8] == std::byte{1});
    HD_CHECK(bytes[12] == std::byte{1});
    HD_CHECK(!hdTrans::dict::CompileSnapshot({temp.File("absent")}, temp.File("bad.hdsnap")));
    HD_CHECK(!std::filesystem::exists(temp.path / "bad.hdsnap"));
    const auto cache = hdTrans::dict::internal::SnapshotPath({FixtureFolder()});
    HD_CHECK(cache.find(".hdsnap") != std::string::npos);
    HD_CHECK(cache != hdTrans::dict::internal::SnapshotPath({FixtureFolder(), FixtureFolder()}));
}

HD_TEST(SnapshotFileRoundTrip) {
    TempFolder temp;
    const std::vector<std::string> folders = {FixtureFolder(), FixtureFolder() + "/over"};
    const auto editable = hdTrans::dict::LoadFoldersTsv(folders);
    HD_REQUIRE(editable.snapshot != nullptr);
    const auto cache = temp.File("cache");
    const auto first = hdTrans::dict::LoadFolders(folders, cache);
    HD_REQUIRE(first.snapshot != nullptr);
    const auto loaded = hdTrans::dict::LoadFolders(folders, cache, true);
    HD_REQUIRE(loaded.snapshot != nullptr);
    HD_CHECK(loaded.snapshot->Mapped());
    HD_CHECK(loaded.snapshot->Healthy());
    HD_CHECK_EQ(loaded.errors.size(), editable.errors.size());
    HD_CHECK(loaded.snapshot->Warnings() == editable.snapshot->Warnings());
    HD_CHECK_EQ(loaded.snapshot->Base().Len(), editable.snapshot->Base().Len());
    for (bool extra : {false, true}) {
        HD_CHECK_EQ(loaded.snapshot->Plain(extra).Entries().size(), editable.snapshot->Plain(extra).Entries().size());
        HD_CHECK_EQ(loaded.snapshot->TextPlain(extra).Entries().size(), editable.snapshot->TextPlain(extra).Entries().size());
        for (const auto pair : {std::pair{&loaded.snapshot->Plain(extra), &editable.snapshot->Plain(extra)},
                                std::pair{&loaded.snapshot->TextPlain(extra), &editable.snapshot->TextPlain(extra)}}) {
            const auto& a = pair.first->Entries();
            const auto& b = pair.second->Entries();
            HD_REQUIRE(a.size() == b.size());
            for (size_t i = 0; i < a.size(); i++) {
                auto fields = [](const hdTrans::dict::UserEntry& e) {
                    return std::tie(e.key, e.target, e.negative, e.prio, e.file, e.line, e.order, e.name, e.nameLayer,
                                    e.kana, e.force, e.hidden, e.finalClass, e.honorific, e.end, e.registerLevel,
                                    e.polite, e.stative, e.euImperative);
                };
                HD_CHECK(fields(a[i]) == fields(b[i]));
                HD_REQUIRE(a[i].codes != nullptr && b[i].codes != nullptr);
                HD_CHECK_EQ(a[i].codes->partText, b[i].codes->partText);
                HD_CHECK(a[i].codes->sem.Text() == b[i].codes->sem.Text());
                HD_CHECK(a[i].codes->gram.Text() == b[i].codes->gram.Text());
                HD_CHECK(a[i].codes->dict.Text() == b[i].codes->dict.Text());
            }
        }
    }
    editable.snapshot->Base().Each([&](std::string_view key, const std::vector<hdTrans::dict::Record>& records) {
        const auto got = loaded.snapshot->Base().Records(key);
        HD_REQUIRE(got.size() == records.size());
        for (size_t i = 0; i < got.size(); i++) {
            HD_CHECK(got[i].target == records[i].target);
            HD_CHECK_EQ(got[i].codes->partText, records[i].codes->partText);
            HD_CHECK(got[i].codes->sem.Text() == records[i].codes->sem.Text());
            HD_CHECK_EQ(got[i].rule, records[i].rule);
            HD_CHECK_EQ(got[i].id, records[i].id);
        }
    });
}


namespace {

std::filesystem::path Utf8Path(std::string_view text) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

bool WriteText(const std::filesystem::path& path, std::string_view text) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f.write(text.data(), static_cast<std::streamsize>(text.size()));
    return f.good();
}

std::vector<std::byte> ReadBytes(const std::string& path) {
    std::ifstream f(Utf8Path(path), std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    const auto bytes = std::as_bytes(std::span(text.data(), text.size()));
    return {bytes.begin(), bytes.end()};
}

bool WriteBytes(const std::string& path, std::span<const std::byte> bytes) {
    return WriteText(Utf8Path(path), {reinterpret_cast<const char*>(bytes.data()), bytes.size()});
}

uint64_t Field(std::span<const std::byte> bytes, size_t offset, size_t width) {
    uint64_t value = 0;
    for (size_t i = 0; i < width; i++) value |= uint64_t{std::to_integer<uint8_t>(bytes[offset + i])} << (8 * i);
    return value;
}

void SetField(std::span<std::byte> bytes, size_t offset, uint64_t value, size_t width) {
    for (size_t i = 0; i < width; i++) bytes[offset + i] = static_cast<std::byte>(value >> (8 * i));
}

void FixDirectories(std::vector<std::byte>& bytes) {
    const auto count = Field(bytes, 40, 4) + Field(bytes, 44, 4);
    SetField(bytes, 196, hdTrans::dict::internal::Crc32c(std::span(bytes).subspan(256, static_cast<size_t>(count * 64))), 4);
    SetField(bytes, 192, 0, 4);
    SetField(bytes, 192, hdTrans::dict::internal::Crc32c(std::span(bytes).first(256)), 4);
}

std::string TargetOf(const hdTrans::dict::Snapshot& snap, std::string_view key) {
    const auto records = snap.Base().Records(key);
    return records.empty() ? std::string() : std::string(records[0].target);
}

}  // namespace

HD_TEST(SnapshotSourceInvalidation) {
    TempFolder temp;
    const auto base = temp.path / "base.tsv";
    const std::string header = "key\tko\tpos\tid\n";
    HD_REQUIRE(WriteText(base, header + "猫\t고양이\tN110\t1\n"));
    const std::vector<std::string> folders{temp.File("")};
    const auto built = hdTrans::dict::LoadFolders(folders);
    HD_REQUIRE(built.snapshot != nullptr);
    const auto cached = hdTrans::dict::LoadFolders(folders, {}, true);
    HD_REQUIRE(cached.snapshot != nullptr && cached.snapshot->Mapped());
    const auto time = std::filesystem::last_write_time(base);
    HD_REQUIRE(WriteText(base, header + "猫\t야옹이\tN110\t1\n"));
    std::filesystem::last_write_time(base, time);
    // Strict verification detects changed bytes even when all file metadata match.
    const auto changed = hdTrans::dict::LoadFolders(folders, {}, true);
    HD_REQUIRE(changed.snapshot != nullptr);
    HD_CHECK(!changed.snapshot->Mapped());
    HD_CHECK_EQ(TargetOf(*changed.snapshot, "猫"), "야옹이");
    const auto current = hdTrans::dict::LoadFolders(folders, {}, true);
    HD_REQUIRE(current.snapshot != nullptr && current.snapshot->Mapped());
    std::filesystem::last_write_time(base, time + std::chrono::seconds(2));
    const auto touched = hdTrans::dict::LoadFolders(folders);
    HD_REQUIRE(touched.snapshot != nullptr);
    HD_CHECK(touched.snapshot->Mapped());
    const auto path = hdTrans::dict::internal::SnapshotPath(folders);
    HD_CHECK(std::filesystem::exists(Utf8Path(path + ".meta")));
    const auto again = hdTrans::dict::LoadFolders(folders);
    HD_REQUIRE(again.snapshot != nullptr);
    HD_CHECK(again.snapshot->Mapped());
    HD_REQUIRE(WriteText(temp.path / "more.tsv", header + "犬\t개\tN110\t2\n"));
    const auto added = hdTrans::dict::LoadFolders(folders, {}, true);
    HD_REQUIRE(added.snapshot != nullptr);
    HD_CHECK(!added.snapshot->Mapped());
    HD_CHECK_EQ(TargetOf(*added.snapshot, "犬"), "개");
    std::filesystem::remove(temp.path / "more.tsv");
    const auto removed = hdTrans::dict::LoadFolders(folders, {}, true);
    HD_REQUIRE(removed.snapshot != nullptr);
    HD_CHECK(!removed.snapshot->Mapped());
    HD_CHECK_EQ(removed.snapshot->Base().Len(), size_t{1});
}

HD_TEST(SnapshotCorruptFileFallback) {
    TempFolder temp;
    const std::vector<std::string> folders{FixtureFolder()};
    const auto cache = temp.File("cache");
    const auto built = hdTrans::dict::LoadFolders(folders, cache);
    HD_REQUIRE(built.snapshot != nullptr);
    const auto path = hdTrans::dict::internal::SnapshotPath(folders, cache);
    const auto good = ReadBytes(path);
    HD_REQUIRE(good.size() > 1024);
    const size_t lengths[] = {0, 7, 255, good.size() / 2, good.size() - 1};
    for (const size_t length : lengths) {
        HD_REQUIRE(WriteBytes(path, std::span(good).first(length)));
        const auto loaded = hdTrans::dict::LoadFolders(folders, cache, true);
        HD_REQUIRE(loaded.snapshot != nullptr);
        HD_CHECK(!loaded.snapshot->Mapped());
        HD_CHECK_EQ(loaded.snapshot->Base().Len(), built.snapshot->Base().Len());
    }
    for (int variant = 0; variant < 6; variant++) {
        auto bytes = good;
        switch (variant) {
        case 0: bytes[0] ^= std::byte{1}; break;
        case 1: SetField(bytes, 8, 99, 4); break;
        case 2: SetField(bytes, 12, 99, 4); break;
        case 3: SetField(bytes, 256 + 8, std::numeric_limits<uint64_t>::max() - 7, 8); break;
        case 4: SetField(bytes, 256 + 36, 3, 4); break;
        case 5: SetField(bytes, 256 + 64 + 8, Field(bytes, 256 + 8, 8), 8); break;
        }
        FixDirectories(bytes);
        HD_REQUIRE(WriteBytes(path, bytes));
        const auto loaded = hdTrans::dict::LoadFolders(folders, cache, true);
        HD_REQUIRE(loaded.snapshot != nullptr);
        HD_CHECK(!loaded.snapshot->Mapped());
        HD_CHECK_EQ(loaded.snapshot->Base().Len(), built.snapshot->Base().Len());
    }
}

HD_TEST(SnapshotLazyChecksumFallback) {
    TempFolder temp;
    const std::vector<std::string> folders{FixtureFolder()};
    const auto cache = temp.File("cache");
    const auto editable = hdTrans::dict::LoadFoldersTsv(folders);
    HD_REQUIRE(editable.snapshot != nullptr);
    const auto built = hdTrans::dict::LoadFolders(folders, cache);
    HD_REQUIRE(built.snapshot != nullptr);
    const auto path = hdTrans::dict::internal::SnapshotPath(folders, cache);
    auto bytes = ReadBytes(path);
    size_t labels = 0;
    for (size_t section = 0; section < Field(bytes, 40, 4); section++) {
        const size_t at = 256 + 64 * section;
        if (Field(bytes, at, 4) == 12 && Field(bytes, at + 4, 4) == 2) labels = static_cast<size_t>(Field(bytes, at + 8, 8));
    }
    HD_REQUIRE(labels != 0);
    bytes[labels] ^= std::byte{1};
    HD_REQUIRE(WriteBytes(path, bytes));
    const auto loaded = hdTrans::dict::LoadFolders(folders, cache);
    HD_REQUIRE(loaded.snapshot != nullptr && loaded.snapshot->Mapped());
    HD_CHECK(loaded.snapshot->Healthy());
    const hdTrans::dict::LookupContext context;
    const auto want = editable.snapshot->GetWordInfo("アリスさん", hdTrans::dict::LookupMode::General, context);
    const auto got = loaded.snapshot->GetWordInfo("アリスさん", hdTrans::dict::LookupMode::General, context);
    HD_CHECK(!loaded.snapshot->Healthy());
    HD_REQUIRE(got.list.size() == want.list.size());
    for (size_t i = 0; i < got.list.size(); i++) HD_CHECK(got.list[i].target == want.list[i].target);
    const auto retry = hdTrans::dict::LoadFolders(folders, cache);
    HD_REQUIRE(retry.snapshot != nullptr);
    HD_CHECK(!retry.snapshot->Mapped());
    HD_CHECK(retry.snapshot->Healthy());
}

HD_TEST(SnapshotCacheErrorsAreOptional) {
    TempFolder temp;
    const auto blocked = temp.File("not-a-directory");
    HD_REQUIRE(WriteText(Utf8Path(blocked), "data"));
    const auto loaded = hdTrans::dict::LoadFolders({FixtureFolder()}, blocked, true);
    HD_REQUIRE(loaded.snapshot != nullptr);
    HD_CHECK(!loaded.snapshot->Mapped());
    HD_CHECK(loaded.snapshot->Healthy());
    const std::string invalid(1, static_cast<char>(0xff));
    const auto bad = hdTrans::dict::LoadFolders({invalid});
    HD_CHECK(bad.snapshot == nullptr);
    HD_REQUIRE(bad.errors.size() == 1);
    HD_CHECK(bad.errors[0].kind == hdTrans::dict::LoadErrorKind::Folder);
    HD_CHECK(!hdTrans::dict::CompileSnapshot({FixtureFolder()}, invalid));
}
