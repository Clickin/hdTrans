#include "hdTrans/dict/snapshot_file.h"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>

#include "hdTrans/dict/mapped_file.h"
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
