#include "core/hdtrans.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace {

int g_failures = 0;

void Check(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "FAIL %s\n", message); ++g_failures; }
}

void Write(const std::filesystem::path& path, const std::string& text) {
    std::ofstream(path, std::ios::binary) << text;
}

std::string Utf8(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

std::string Translate(const std::string& text) {
    char* output = nullptr;
    std::size_t size = 0;
    Check(hdTrans_Translate(text.data(), text.size(), &output, &size) == 0, "translate");
    const std::string result = output ? std::string(output, size) : std::string();
    hdTrans_FreeMem(output);
    return result;
}

std::string Errors() {
    char* output = nullptr;
    std::size_t size = 0;
    Check(hdTrans_UserDictError(&output, &size) == 0, "diagnostics");
    const std::string result = output ? std::string(output, size) : std::string();
    hdTrans_FreeMem(output);
    return result;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 1) return 2;
    std::error_code error;
    const auto folder = std::filesystem::canonical(argv[0], error).parent_path();
    if (error) return 2;
    const auto config = folder / "hdtrans.config.yaml";
    if (std::filesystem::exists(config, error)) return 2;
    const auto fixtures = folder / "fixtures";
    std::filesystem::create_directories(fixtures / "first", error);
    std::filesystem::create_directories(fixtures / "second", error);
    std::filesystem::create_directories(fixtures / "Ehnd", error);
    const std::string header = "key\tko\tpos\tsem\tgram\tdict\trule\tflags\tprio\treading\tid\n";
    Write(fixtures / "first/base.tsv", header + "猫\t고양이\tN110\t\t\tGN\t\t\t\t\t1\n");
    Write(fixtures / "second/base.tsv", header + "猫\t야옹이\tN110\t\t\tGN\t\t\t\t\t1\n");
    Write(fixtures / "user.tsv", "犬\t멍멍이\n");
    Write(fixtures / "Ehnd/UserDict_test.txt", "タマ\t나비\t1\n");
    Write(config, "dicts: fixtures/first\nuserdict: fixtures/user.tsv\nehnd: fixtures/Ehnd\ncache: off\n"
                  "cg: false\nmisread: true\nreference: false\n");
    Check(hdTrans_Initialize(nullptr) == 0, "initialize with config dictionaries");
    Check(Translate("猫") == "고양이", "configured dictionary");
    Check(Translate("犬") == "멍멍이", "configured user dictionary");
    Check(Translate("タマ") == "나비", "configured Ehnd dictionary");
    Check(Errors().empty(), "valid config diagnostics");
    Write(config, "dicts: fixtures/second\nuserdict: ''\nehnd: ''\ncache: off\nunknown: value\n");
    Check(hdTrans_Reload() == 0, "reload config");
    Check(Translate("猫") == "야옹이", "reloaded dictionary");
    Check(Errors().find("hdtrans.config.yaml:5: unknown key: unknown") != std::string::npos, "config diagnostic path and line");
    Write(config, "dicts: fixtures/missing\ncache: off\n");
    Check(hdTrans_Reload() == -1, "failed reload");
    Check(Translate("猫") == "야옹이", "failed reload retains dictionaries");
    hdTrans_Terminate();

    Write(config, "dicts: fixtures/second\nuserdict: ''\nehnd: ''\ncache: cache\n");
    std::string explicitDirs = Utf8(fixtures / "first");
    explicitDirs.append(2, '\0');
    Check(hdTrans_Initialize(explicitDirs.c_str()) == 0, "initialize explicit directories");
    Check(Translate("猫") == "고양이", "API directories override config");
    std::filesystem::remove(config, error);
    Check(hdTrans_Reload() == 0, "reload without optional config");
    Check(Translate("猫") == "고양이", "no config preserves API dictionaries");
    Check(Errors().empty(), "removed config clears diagnostics");
    const char* cache = std::getenv("HDTRANS_CACHE_DIR");
    Check(cache && std::string(cache) == "off", "removed config restores cache environment");
    hdTrans_Terminate();
    std::filesystem::remove_all(fixtures, error);
    return g_failures == 0 ? 0 : 1;
}
