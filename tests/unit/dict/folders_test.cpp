#include "core/dict/folders.h"

#include <chrono>
#include <filesystem>
#include <string>
#include <system_error>

#include "hdtest.h"

using namespace hdTrans::dict;

namespace {

std::filesystem::path Path(const std::string& utf8) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

std::string Utf8(const std::filesystem::path& p) {
    const std::u8string s = p.u8string();
    return std::string(reinterpret_cast<const char*>(s.data()), s.size());
}

}  // namespace

HD_TEST(DictFoldersOfProgram) {
    std::error_code ec;
    const auto dir = std::filesystem::temp_directory_path(ec) /
                     ("hdtrans-folders-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    HD_REQUIRE(std::filesystem::create_directory(dir, ec));
    const std::string program = Utf8(dir);

    HD_CHECK(Path(ShippedFolder(program)) == dir / "dicts");
    HD_CHECK(Path(UserFile(program)) == dir / "userdict.tsv");
    HD_CHECK_EQ(UserFile(""), "");
    // Without dicts next to the program, dicts in the current directory.
    HD_CHECK_EQ(DefaultFolder(program), "dicts");
    HD_CHECK_EQ(DefaultFolder(""), "dicts");
    std::filesystem::create_directory(dir / "dicts", ec);
    HD_REQUIRE(!ec);
    HD_CHECK(Path(DefaultFolder(program)) == dir / "dicts");

    std::filesystem::remove_all(dir, ec);
}
