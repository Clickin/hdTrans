#include "core/hdtrans.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#endif

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


#ifdef _WIN32
template<class Function>
Function Procedure(HMODULE module, const char* name) {
    const FARPROC address = GetProcAddress(module, name);
    Function function = nullptr;
    static_assert(sizeof(function) == sizeof(address));
    std::memcpy(&function, &address, sizeof(function));
    return function;
}

void CheckJ2K(const std::filesystem::path& source, const std::filesystem::path& folder) {
    std::error_code error;
    const auto dll = folder / "J2KEngine.dll";
    std::filesystem::copy_file(source, dll, std::filesystem::copy_options::overwrite_existing, error);
    Check(!error, "copy J2K DLL to isolated config folder");
    if (error) return;
    const HMODULE module = LoadLibraryW(dll.c_str());
    Check(module != nullptr, "load J2K config fixture");
    if (!module) return;
    const auto initialize = Procedure<int (__stdcall*)(char*)>(module, "J2K_Initialize");
    const auto reload = Procedure<void (__stdcall*)()>(module, "J2K_ReloadUserDict");
    const auto terminate = Procedure<int (__stdcall*)()>(module, "J2K_Terminate");
    const auto translate = Procedure<wchar_t* (__stdcall*)(HWND, const wchar_t*)>(module, "J2K_TranslateMMNTW");
    const auto freeMem = Procedure<void (__stdcall*)(void*)>(module, "J2K_FreeMem");
    Check(initialize && reload && terminate && translate && freeMem, "J2K config exports");
    if (initialize && reload && terminate && translate && freeMem) {
        const auto config = folder / "hdtrans.config.yaml";
        const std::string base = "dicts: fixtures/first\nuserdict: ''\ncache: off\n";
        auto checkTranslation = [&](const wchar_t* input, const wchar_t* expected, const char* message) {
            wchar_t* output = translate(nullptr, input);
            Check(output && std::wstring(output) == expected, message);
            freeMem(output);
        };
        Write(config, base);
        Check(initialize(nullptr) == 1, "J2K config initialize");
        checkTranslation(L"猫", L"고양이", "J2K ignores sibling Ehnd by default");
        Write(config, base + "ehnd: true\n");
        reload();
        checkTranslation(L"猫", L"검은고양이", "J2K enables sibling Ehnd on reload");
        Write(config, base + "ehnd: false\n");
        reload();
        checkTranslation(L"猫", L"고양이", "J2K disables Ehnd on reload");
        Write(config, base + "ehnd: fixtures/Ehnd\n");
        reload();
        checkTranslation(L"タマ", L"나비", "J2K configured Ehnd path");
        terminate();
        std::filesystem::remove(config, error);
        std::filesystem::create_directories(folder / "dicts", error);
        std::filesystem::copy_file(folder / "fixtures/first/base.tsv", folder / "dicts/base.tsv",
                                   std::filesystem::copy_options::overwrite_existing, error);
        Check(initialize(nullptr) == 1, "J2K initialize without config");
        checkTranslation(L"猫", L"고양이", "J2K without config ignores sibling Ehnd");
        terminate();
        std::filesystem::remove_all(folder / "dicts", error);
        const auto data = folder / "HDTRANS";
        const auto user = data / "USER";
        std::filesystem::create_directories(data / "DiCtS", error);
        std::filesystem::create_directories(user, error);
        std::filesystem::copy_file(folder / "fixtures/first/base.tsv", data / "DiCtS/base.tsv",
                                   std::filesystem::copy_options::overwrite_existing, error);
        Write(user / "HDTRANS.CONFIG.YAML", "cache: off\n");
        Write(user / "USERDICT.TSV", "犬\t새사전\n");
        Check(initialize(nullptr) == 1, "J2K nested case-insensitive layout");
        checkTranslation(L"猫", L"고양이", "J2K nested default dictionary");
        checkTranslation(L"犬", L"새사전", "J2K nested user dictionary");
        Write(user / "HDTRANS.CONFIG.YAML", "ehnd: true\ncache: off\n");
        reload();
        checkTranslation(L"猫", L"검은고양이", "J2K nested config keeps outer Ehnd location");
        terminate();
        std::filesystem::remove_all(data, error);
    }
    FreeLibrary(module);
    std::filesystem::remove(dll, error);
}
#endif

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    if (argc != 2) return 2;
#else
    if (argc != 1) return 2;
#endif
    std::error_code error;
    const auto folder = std::filesystem::canonical(argv[0], error).parent_path();
    if (error) return 2;
    const auto config = folder / "hdtrans.config.yaml";
    if (std::filesystem::exists(config, error)) return 2;
    const auto fixtures = folder / "fixtures";
    std::filesystem::create_directories(folder / "Ehnd", error);
    Write(folder / "Ehnd/PostFilter_test.txt", "고양이\t검은고양이\t1\t0\n");
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
    Check(Translate("猫") == "고양이", "API directories override config; sibling Ehnd is opt-in");
    Write(config, "dicts: fixtures/second\nuserdict: ''\nehnd: true\ncache: cache\n");
    Check(hdTrans_Reload() == 0, "enable sibling Ehnd on reload");
    Check(Translate("猫") == "검은고양이", "ehnd true uses sibling folder");
    Write(config, "dicts: fixtures/second\nuserdict: ''\nehnd: false\ncache: cache\n");
    Check(hdTrans_Reload() == 0, "disable Ehnd on reload");
    Check(Translate("猫") == "고양이", "ehnd false disables sibling folder");
    std::filesystem::remove(config, error);
    Check(hdTrans_Reload() == 0, "reload without optional config");
    Check(Translate("猫") == "고양이", "no config preserves API dictionaries");
    Check(Errors().empty(), "removed config clears diagnostics");
    const char* cache = std::getenv("HDTRANS_CACHE_DIR");
    Check(cache && std::string(cache) == "off", "removed config restores cache environment");
    hdTrans_Terminate();
    const auto data = folder / "HDTRANS";
    const auto user = data / "USER";
    std::filesystem::create_directories(data / "DiCtS", error);
    std::filesystem::create_directories(user, error);
    std::filesystem::copy_file(fixtures / "first/base.tsv", data / "DiCtS/base.tsv",
                               std::filesystem::copy_options::overwrite_existing, error);
    Write(user / "USERDICT.TSV", "犬\t새사전\n");
    Write(user / "HDTRANS.CONFIG.YAML", "dicts: ../DiCtS\ncache: off\n");
    Check(hdTrans_Initialize(nullptr) == 0, "initialize case-insensitive nested layout");
    Check(Translate("猫") == "고양이", "nested dictionary path relative to user config");
    Check(Translate("犬") == "새사전", "case-insensitive nested user dictionary");
    hdTrans_Terminate();
    std::filesystem::remove_all(data, error);
    std::filesystem::create_directories(folder / "user", error);
    Write(folder / "user/hdtrans.config.yaml", "dicts: ../fixtures/first\ncache: off\n");
    Write(folder / "user/userdict.tsv", "犬\t새사전\n");
    Check(hdTrans_Initialize(nullptr) == 0, "initialize flat user layout");
    Check(Translate("犬") == "새사전", "flat user dictionary");
    hdTrans_Terminate();
    std::filesystem::remove_all(folder / "user", error);
#ifdef _WIN32
    CheckJ2K(argv[1], folder);
#endif
    std::filesystem::remove_all(fixtures, error);
    std::filesystem::remove_all(folder / "Ehnd", error);
    return g_failures == 0 ? 0 : 1;
}
