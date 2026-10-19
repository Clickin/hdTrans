#include <windows.h>

#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>

namespace {

using InitializeExFn = int(__stdcall*)(const char*, const char*);
using TranslateFn = char*(__stdcall*)(int, const char*);
using TranslateWFn = wchar_t*(__stdcall*)(int, const wchar_t*);
using FreeMemFn = void(__stdcall*)(void*);

constexpr const char* kExports[] = {
    "J2K_FreeMem", "J2K_GetPriorDict", "J2K_GetProperty", "J2K_Initialize", "J2K_InitializeEx",
    "J2K_ReloadUserDict", "J2K_SetDelJPN", "J2K_SetField", "J2K_SetHnj2han", "J2K_SetJWin",
    "J2K_SetPriorDict", "J2K_SetProperty", "J2K_StopTranslation", "J2K_Terminate", "J2K_TranslateChat",
    "J2K_TranslateFM", "J2K_TranslateMM", "J2K_TranslateMMEx", "J2K_TranslateMMNT", "J2K_TranslateMMNTW",
};

struct Sample {
    const wchar_t* input;
    const wchar_t* expected;
};

constexpr Sample kSamples[] = {
    {L"こんにちは", L"안녕하세요"},
    {L"日本語", L"일본어"},
};

bool Check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    return ok;
}

template <class Fn>
Fn Export(HMODULE module, const char* name) {
    const auto address = GetProcAddress(module, name);
    if (!address) std::printf("FAIL missing export: %s\n", name);
    return reinterpret_cast<Fn>(address);
}

std::string Encode(const wchar_t* text, UINT codePage) {
    const int size = WideCharToMultiByte(codePage, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(codePage, 0, text, -1, result.data(), size, nullptr, nullptr);
    result.pop_back();
    return result;
}

bool TranslateWide(TranslateWFn translate, FreeMemFn freeMem) {
    bool ok = true;
    for (int round = 0; round < 16; ++round) {
        for (const auto& sample : kSamples) {
            wchar_t* output = translate(0, sample.input);
            const bool returned = output != nullptr;
            const std::wstring copied = output ? output : L"";
            if (output) freeMem(output);
            const bool matches = returned && copied == sample.expected;
            if (!matches) std::printf("FAIL UTF-16 translation at round %d\n", round);
            ok = matches && ok;
        }
    }
    return Check(ok, "UTF-16 results copied and released with J2K_FreeMem");
}

bool TranslateAnsi(TranslateFn translate, FreeMemFn freeMem) {
    bool ok = true;
    for (int round = 0; round < 16; ++round) {
        for (const auto& sample : kSamples) {
            const std::string input = Encode(sample.input, 932);
            const std::string expected = Encode(sample.expected, 949);
            char* output = translate(0, input.c_str());
            const bool returned = output != nullptr;
            const std::string copied = output ? output : "";
            if (output) freeMem(output);
            const bool matches = returned && !expected.empty() && expected == copied;
            if (!matches) std::printf("FAIL CP932/CP949 translation at round %d\n", round);
            ok = matches && ok;
        }
    }
    return Check(ok, "CP932 input, CP949 results copied and released with J2K_FreeMem");
}

bool RunNative(HMODULE module, const std::wstring& missingDat, bool ansi, bool narrowPath) {
    FARPROC addresses[20]{};
    bool found = true;
    for (size_t index = 0; index < 20; ++index) {
        addresses[index] = GetProcAddress(module, kExports[index]);
        if (index < 19 && !addresses[index]) {
            std::printf("FAIL missing export: %s\n", kExports[index]);
            found = false;
        }
    }
    if (!Check(found, "all 19 mandatory exports resolve")) return false;
    if (!ansi && !Check(addresses[19] != nullptr, "optional Unicode export selects the Unicode branch")) return false;
    if (ansi) std::puts("ANSI fallback forced; the shipped DLL normally selects Unicode");
    Sleep(50);
    std::string datPath;
    if (narrowPath) {
        for (wchar_t value : missingDat) datPath.push_back(static_cast<char>(value));
    } else {
        datPath = Encode(missingDat.c_str(), CP_ACP);
    }
    const auto initialize = reinterpret_cast<InitializeExFn>(addresses[4]);
    if (!Check(initialize("CSUSER123455", datPath.c_str()) != 0, "InitializeEx(key, missing Dat) loads DLL-adjacent dicts"))
        return false;
    const auto freeMem = reinterpret_cast<FreeMemFn>(addresses[0]);
    if (ansi) return TranslateAnsi(reinterpret_cast<TranslateFn>(addresses[18]), freeMem);
    return TranslateWide(reinterpret_cast<TranslateWFn>(addresses[19]), freeMem);
}

bool RunServer(HMODULE module, const std::wstring& missingDat) {
    const auto initialize = Export<InitializeExFn>(module, "J2K_InitializeEx");
    if (!initialize) return false;
    const std::string datPath = Encode(missingDat.c_str(), CP_ACP);
    if (!Check(initialize("CSUSER123455", datPath.c_str()) != 0, "InitializeEx(key, missing Dat) loads DLL-adjacent dicts"))
        return false;
    const auto freeMem = Export<FreeMemFn>(module, "J2K_FreeMem");
    const auto translate = Export<TranslateWFn>(module, "J2K_TranslateMMNTW");
    if (!freeMem || !translate) return false;
    return TranslateWide(translate, freeMem);
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc != 3) {
        std::puts("usage: tool_compat <luna|luna-ansi|anemone|anemone-ansi|eztransserver> <J2KEngine.dll>");
        return 2;
    }
    wchar_t cwd[MAX_PATH]{};
    if (!GetCurrentDirectoryW(MAX_PATH, cwd)) return 1;
    const std::wstring missingDat = std::wstring(cwd) + L"\\missing\\Dat";
    if (!Check(GetFileAttributesW(missingDat.c_str()) == INVALID_FILE_ATTRIBUTES, "host Dat path does not exist")) return 1;
    if (!Check(GetFileAttributesW(L"dicts") == INVALID_FILE_ATTRIBUTES, "working directory has no dictionaries")) return 1;
    SetEnvironmentVariableW(L"HDTRANS_DICTS", L"missing-dictionary-override");
    const HMODULE module = LoadLibraryW(argv[2]);
    if (!Check(module != nullptr, "LoadLibraryW")) return 1;
    bool ok = false;
    if (std::wcscmp(argv[1], L"luna") == 0 || std::wcscmp(argv[1], L"luna-ansi") == 0) {
        ok = RunNative(module, missingDat, std::wcscmp(argv[1], L"luna-ansi") == 0, false);
    } else if (std::wcscmp(argv[1], L"anemone") == 0 || std::wcscmp(argv[1], L"anemone-ansi") == 0) {
        ok = RunNative(module, missingDat, std::wcscmp(argv[1], L"anemone-ansi") == 0, true);
    } else if (std::wcscmp(argv[1], L"eztransserver") == 0) {
        ok = RunServer(module, missingDat);
    }
    std::puts("No J2K_Terminate or FreeLibrary; module lifetime ends with the process");
    return ok ? 0 : 1;
}
