#include "core/dict/folders.h"

#include <chrono>
#include <filesystem>
#include <fstream>
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
    HD_CHECK(Path(UserFile(program)) == dir / "user/userdict.tsv");
    HD_CHECK_EQ(UserFile(""), "");
    // Without dicts next to the program, dicts in the current directory.
    HD_CHECK_EQ(DefaultFolder(program), "dicts");
    HD_CHECK_EQ(DefaultFolder(""), "dicts");
    std::filesystem::create_directory(dir / "dicts", ec);
    HD_REQUIRE(!ec);
    HD_CHECK(Path(DefaultFolder(program)) == dir / "dicts");
    // The Ehnd folder only when it is there.
    HD_CHECK_EQ(EhndFolder(program), "");
    HD_CHECK_EQ(EhndFolder(""), "");
    std::filesystem::create_directory(dir / "Ehnd", ec);
    HD_REQUIRE(!ec);
    HD_CHECK(Path(EhndFolder(program)) == dir / "Ehnd");

    std::filesystem::remove_all(dir, ec);
}

HD_TEST(DictDataLayouts) {
    std::error_code error;
    const auto root = std::filesystem::temp_directory_path(error) /
        ("hdtrans-layouts-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const struct Case { const char* data; const char* user; const char* dicts; const char* userdict; const char* config; } cases[] = {
        {"", "", "dicts", "userdict.tsv", "hdtrans.config.yaml"},
        {"", "user", "dicts", "userdict.tsv", "hdtrans.config.yaml"},
        {"hdTrans", "user", "dicts", "userdict.tsv", "hdtrans.config.yaml"},
        {"hdtrans", "USER", "DICTS", "USERDICT.TSV", "HDTRANS.CONFIG.YAML"},
        {"HDTRANS", "User", "Dicts", "UserDict.tsv", "HdTrans.Config.yaml"},
    };
    std::size_t index = 0;
    for (const auto& test : cases) {
        const auto program = root / std::to_string(index++);
        const auto data = test.data[0] ? program / test.data : program;
        const auto user = test.user[0] ? data / test.user : data;
        std::filesystem::create_directories(program, error);
        if (!test.data[0]) std::ofstream(program / "hdtrans") << "executable";
        std::filesystem::create_directories(data / test.dicts, error);
        HD_REQUIRE(!error);
        std::filesystem::create_directories(user, error);
        HD_REQUIRE(!error);
        std::ofstream(user / test.userdict) << "# user dictionary\n";
        std::ofstream(user / test.config) << "cg: true\n";
        std::filesystem::create_directory(program / "EHND", error);
        HD_REQUIRE(!error);
        const auto path = Utf8(program);
        HD_CHECK(Path(DataFolder(path)) == data);
        HD_CHECK(Path(UserFolder(path)) == (test.user[0] ? user : data / "user"));
        HD_CHECK(Path(ShippedFolder(path)) == data / test.dicts);
        HD_CHECK(Path(DefaultFolder(path)) == data / test.dicts);
        HD_CHECK(Path(UserFile(path)) == user / test.userdict);
        HD_CHECK(Path(ConfigFile(path)) == user / test.config);
        HD_CHECK(Path(EhndFolder(path)) == program / "EHND");
        HD_CHECK(Path(FindChild(path, "missing")) == program / "missing");
        if (test.data[0]) {
            std::filesystem::create_directory(program / "dicts", error);
            std::ofstream(program / "userdict.tsv") << "# legacy\n";
            std::ofstream(program / "hdtrans.config.yaml") << "cg: false\n";
            HD_CHECK(Path(ShippedFolder(path)) == data / test.dicts);
            HD_CHECK(Path(UserFile(path)) == user / test.userdict);
            HD_CHECK(Path(ConfigFile(path)) == user / test.config);
        }
    }
    std::filesystem::remove_all(root, error);
}
