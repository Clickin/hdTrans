#include <windows.h>

#include <barrier>
#include <cstdio>
#include <cwchar>
#include <string>
#include <thread>

namespace {

using InitializeExFn = int(__stdcall*)(const char*, const char*);
using TranslateFn = char*(__stdcall*)(int, const char*);
using TranslateWFn = wchar_t*(__stdcall*)(int, const wchar_t*);
using FreeMemFn = void(__stdcall*)(void*);
using FreeMemIntFn = int(__stdcall*)(char*);
using StopFn = int(__stdcall*)(int);
using TerminateFn = int(__stdcall*)();

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

template <class Free>
bool TranslateAnsi(TranslateFn translate, Free freeMem) {
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

struct AralEngine {
    HMODULE module = nullptr;
    InitializeExFn initialize = nullptr;
    TranslateFn translate = nullptr;
    FreeMemIntFn freeMem = nullptr;
    StopFn stop = nullptr;
    std::wstring datPath;
};

bool LoadAral(const std::wstring& dllPath, AralEngine& engine) {
    const size_t slash = dllPath.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return false;
    engine.datPath = dllPath.substr(0, slash + 1) + L"Dat";
    for (wchar_t& value : engine.datPath) {
        if (value == L'/') value = L'\\';
    }
    if (!Check(GetFileAttributesW(engine.datPath.c_str()) == INVALID_FILE_ATTRIBUTES, "DLL-adjacent Dat does not exist"))
        return false;
    const std::wstring loadPath = engine.datPath + L"\\..\\J2KEngine.dll";
    engine.module = LoadLibraryW(loadPath.c_str());
    if (!Check(engine.module != nullptr, "LoadLibraryW through Dat\\..\\J2KEngine.dll without a Dat folder")) return false;
    engine.initialize = Export<InitializeExFn>(engine.module, "J2K_InitializeEx");
    engine.translate = Export<TranslateFn>(engine.module, "J2K_TranslateMMNT");
    engine.freeMem = Export<FreeMemIntFn>(engine.module, "J2K_FreeMem");
    engine.stop = Export<StopFn>(engine.module, "J2K_StopTranslation");
    const auto terminate = Export<TerminateFn>(engine.module, "J2K_Terminate");
    return engine.initialize && engine.translate && engine.freeMem && engine.stop && terminate;
}

bool TranslateAral(AralEngine& engine) {
    const std::string datPath = Encode(engine.datPath.c_str(), CP_ACP);
    const bool initialized = Check(engine.initialize("CSUSER123455", datPath.c_str()) != 0,
                                   "worker InitializeEx(key, ANSI Dat)");
    return TranslateAnsi(engine.translate, engine.freeMem) && initialized;
}

bool RunAral(const std::wstring& dllPath, bool splitThreads) {
    bool ok = false;
    if (splitThreads) {
        AralEngine callerEngine;
        if (!LoadAral(dllPath, callerEngine)) return false;
        std::thread worker([&callerEngine, &ok] { ok = TranslateAral(callerEngine); });
        worker.join();
        callerEngine.stop(0);
        std::puts("PASS caller StopTranslation(0) after the worker exits");
    } else {
        std::thread worker([&dllPath, &ok] {
            AralEngine workerEngine;
            if (!LoadAral(dllPath, workerEngine)) return;
            ok = TranslateAral(workerEngine);
            workerEngine.stop(0);
            std::puts("PASS worker StopTranslation(0)");
        });
        worker.join();
    }
    std::puts("No J2K_Terminate or FreeLibrary; module lifetime ends with the process");
    return ok;
}

bool RunWeb(HMODULE module) {
    const auto initialize = Export<InitializeExFn>(module, "J2K_InitializeEx");
    const auto translate = Export<TranslateWFn>(module, "J2K_TranslateMMNTW");
    if (!initialize || !translate) return false;
    constexpr char datPath[] = R"(C:\Program Files (x86)\ChangShinSoft\ezTrans XP\Dat)";
    if (!Check(GetFileAttributesA(datPath) == INVALID_FILE_ATTRIBUTES, "hardcoded Dat path does not exist")) return false;
    if (!Check(initialize("CSUSER123455", datPath) != 0, "InitializeEx with the hardcoded ANSI Dat path")) return false;
    bool correct[2]{true, true};
    std::thread workers[2];
    std::barrier<> start(2);
    for (size_t workerIndex = 0; workerIndex < 2; ++workerIndex) {
        workers[workerIndex] = std::thread([translate, &correct, &start, workerIndex] {
            start.arrive_and_wait();
            for (int round = 0; round < 16; ++round) {
                for (const auto& sample : kSamples) {
                    const wchar_t* output = translate(0, sample.input);
                    const std::wstring copied = output ? output : L"";
                    if (!output || copied != sample.expected) correct[workerIndex] = false;
                }
            }
        });
    }
    for (auto& worker : workers) worker.join();
    std::puts("No J2K_FreeMem call; returned buffers remain allocated");
    return Check(correct[0] && correct[1], "two unlocked callers copy UTF-16 results concurrently");
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc != 3) {
        std::puts("usage: tool_compat <luna|luna-ansi|anemone|anemone-ansi|aral02|aral03|eztransserver|eztransweb> <DLL>");
        return 2;
    }
    wchar_t cwd[MAX_PATH]{};
    if (!GetCurrentDirectoryW(MAX_PATH, cwd)) return 1;
    const std::wstring missingDat = std::wstring(cwd) + L"\\missing\\Dat";
    if (!Check(GetFileAttributesW(missingDat.c_str()) == INVALID_FILE_ATTRIBUTES, "host Dat path does not exist")) return 1;
    if (!Check(GetFileAttributesW(L"dicts") == INVALID_FILE_ATTRIBUTES, "working directory has no dictionaries")) return 1;
    SetEnvironmentVariableW(L"HDTRANS_DICTS", L"missing-dictionary-override");
    if (std::wcscmp(argv[1], L"aral02") == 0 || std::wcscmp(argv[1], L"aral03") == 0) {
        return RunAral(argv[2], std::wcscmp(argv[1], L"aral03") == 0) ? 0 : 1;
    }
    const HMODULE module = LoadLibraryW(argv[2]);
    if (!Check(module != nullptr, "LoadLibraryW")) return 1;
    bool ok = false;
    if (std::wcscmp(argv[1], L"luna") == 0 || std::wcscmp(argv[1], L"luna-ansi") == 0) {
        ok = RunNative(module, missingDat, std::wcscmp(argv[1], L"luna-ansi") == 0, false);
    } else if (std::wcscmp(argv[1], L"anemone") == 0 || std::wcscmp(argv[1], L"anemone-ansi") == 0) {
        ok = RunNative(module, missingDat, std::wcscmp(argv[1], L"anemone-ansi") == 0, true);
    } else if (std::wcscmp(argv[1], L"eztransserver") == 0) {
        ok = RunServer(module, missingDat);
    } else if (std::wcscmp(argv[1], L"eztransweb") == 0) {
        ok = RunWeb(module);
    }
    std::puts("No J2K_Terminate or FreeLibrary; module lifetime ends with the process");
    return ok ? 0 : 1;
}
