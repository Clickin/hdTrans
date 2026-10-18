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

