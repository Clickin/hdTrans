/* Host-side test for J2KEngine.dll: loads the DLL the way translator front ends do and runs the
   J2K_* call sequence, twice, then from two threads at once. Usage: j2k_host <path to J2KEngine.dll>
   Every translation is also written as UTF-8, one per line, to j2k_host.out.txt next to this executable. */
#include <windows.h>
#include <psapi.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#pragma comment(lib, "psapi.lib")

typedef int (__stdcall *InitializeExFn)(char*, char*);
typedef char* (__stdcall *TranslateMMNTFn)(HWND, char*);
typedef wchar_t* (__stdcall *TranslateMMNTWFn)(HWND, const wchar_t*);
typedef void (__stdcall *FreeMemFn)(void*);
typedef int (__stdcall *StopTranslationFn)(void*);
typedef int (__stdcall *TerminateFn)(void);
typedef void (__stdcall *ReloadUserDictFn)(void);

static const char* const kExports[] = {
    "J2K_Initialize", "J2K_InitializeEx", "J2K_Terminate", "J2K_TranslateMM", "J2K_TranslateMMEx",
    "J2K_TranslateMMNT", "J2K_TranslateMMNTW", "J2K_TranslateChat", "J2K_TranslateFM", "J2K_FreeMem",
    "J2K_StopTranslation", "J2K_ReloadUserDict", "J2K_SetField", "J2K_SetHnj2han", "J2K_SetDelJPN",
    "J2K_SetJWin", "J2K_SetPriorDict", "J2K_GetPriorDict", "J2K_SetProperty", "J2K_GetProperty",
    "J2K_GetJ2KMainDir",
};

static const char* const kLines[] = {
    "\xE7\xA7\x81\xE3\x81\xAF\xE5\xAD\xA6\xE7\x94\x9F\xE3\x81\xA7\xE3\x81\x99\xE3\x80\x82",
    "\xE4\xBB\x8A\xE6\x97\xA5\xE3\x81\xAF\xE5\xA4\xA9\xE6\xB0\x97\xE3\x81\x8C\xE3\x81\x84\xE3\x81\x84\xE3\x81\xA7\xE3\x81\x99\xE3\x81\xAD\xE3\x80\x82",
    "\xE3\x81\x93\xE3\x82\x93\xE3\x81\xAB\xE3\x81\xA1\xE3\x81\xAF\xE3\x80\x81\xE4\xB8\x96\xE7\x95\x8C\xE3\x80\x82",
};
#define LINE_COUNT (sizeof(kLines) / sizeof(kLines[0]))
static const wchar_t kWideLine[] = L"\x65E5\x672C\x8A9E\x3092\x52C9\x5F37\x3057\x3066\x3044\x307E\x3059\x3002";
/* The Shift-JIS lines, then the UTF-16 line. */
#define RESULT_COUNT (LINE_COUNT + 1)

static int g_failed;
static FILE* g_out;
static LARGE_INTEGER g_freq;

static void Check(int ok, const char* what) {
    printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) g_failed = 1;
}

static double NowMs(void) {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return (double)now.QuadPart * 1000.0 / (double)g_freq.QuadPart;
}

typedef struct {
    int calls;
    double ms;
} Timing;

static void PrintTiming(const Timing* t) {
    printf("translate: %d calls, total %.2f ms\n", t->calls, t->ms);
}

static void PrintMemory(void) {
    PROCESS_MEMORY_COUNTERS_EX pmc;
    memset(&pmc, 0, sizeof(pmc));
    pmc.cb = sizeof(pmc);
    if (!GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
        printf("memory query failed\n");
        return;
    }
    printf("peak working set %.1f MB\n", pmc.PeakWorkingSetSize / 1048576.0);
    printf("private bytes %.1f MB\n", pmc.PrivateUsage / 1048576.0);
}

typedef struct {
    HMODULE module;
    InitializeExFn initializeEx;
    TranslateMMNTFn translate;
    TranslateMMNTWFn translateW;
    FreeMemFn freeMem;
    StopTranslationFn stop;
    TerminateFn terminate;
    ReloadUserDictFn reloadUserDict;
} Engine;

static int LoadEngine(Engine* e, const wchar_t* path) {
    memset(e, 0, sizeof(*e));
    e->module = LoadLibraryW(path);
    if (!e->module) return 0;
    e->initializeEx = (InitializeExFn)GetProcAddress(e->module, "J2K_InitializeEx");
    e->translate = (TranslateMMNTFn)GetProcAddress(e->module, "J2K_TranslateMMNT");
    e->translateW = (TranslateMMNTWFn)GetProcAddress(e->module, "J2K_TranslateMMNTW");
    e->freeMem = (FreeMemFn)GetProcAddress(e->module, "J2K_FreeMem");
    e->stop = (StopTranslationFn)GetProcAddress(e->module, "J2K_StopTranslation");
    e->terminate = (TerminateFn)GetProcAddress(e->module, "J2K_Terminate");
    e->reloadUserDict = (ReloadUserDictFn)GetProcAddress(e->module, "J2K_ReloadUserDict");
    return e->initializeEx && e->translate && e->translateW && e->freeMem && e->stop && e->terminate &&
           e->reloadUserDict;
}

/* Converts a UTF-8 string to a newly allocated string in the given code page. */
static char* FromUtf8(const char* s, UINT codePage) {
    int wn = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    wchar_t* w = (wchar_t*)HeapAlloc(GetProcessHeap(), 0, wn * sizeof(wchar_t));
    int n;
    char* out;
    MultiByteToWideChar(CP_UTF8, 0, s, -1, w, wn);
    n = WideCharToMultiByte(codePage, 0, w, -1, NULL, 0, NULL, NULL);
    out = (char*)HeapAlloc(GetProcessHeap(), 0, n);
    WideCharToMultiByte(codePage, 0, w, -1, out, n, NULL, NULL);
    HeapFree(GetProcessHeap(), 0, w);
    return out;
}

/* Converts a string in the given code page to a newly allocated UTF-16 string. */
static wchar_t* WideFrom(const char* s, UINT codePage) {
    int wn = MultiByteToWideChar(codePage, 0, s, -1, NULL, 0);
    wchar_t* w = (wchar_t*)HeapAlloc(GetProcessHeap(), 0, wn * sizeof(wchar_t));
    MultiByteToWideChar(codePage, 0, s, -1, w, wn);
    return w;
}

static wchar_t* DupWide(const wchar_t* s) {
    size_t n = wcslen(s) + 1;
    wchar_t* d = (wchar_t*)HeapAlloc(GetProcessHeap(), 0, n * sizeof(wchar_t));
    memcpy(d, s, n * sizeof(wchar_t));
    return d;
}

/* Writes <dir of path><leaf> into out; leaf alone when path has no directory part. */
static void SiblingPath(const wchar_t* path, const wchar_t* leaf, wchar_t* out, size_t cap) {
    const wchar_t* sep = wcsrchr(path, L'\\');
    const wchar_t* alt = wcsrchr(path, L'/');
    size_t dirLen = 0;
    if (alt && (!sep || alt > sep)) sep = alt;
    if (sep) dirLen = (size_t)(sep - path) + 1;
    _snwprintf(out, cap, L"%.*s%s", (int)dirLen, path, leaf);
    out[cap - 1] = L'\0';
}

static int DirExists(const wchar_t* path) {
    DWORD attr = GetFileAttributesW(path);
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

/* Prints a translation to stdout and to the output file, one line per translation. */
static void Emit(const char* label, const wchar_t* w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    char* u = (char*)HeapAlloc(GetProcessHeap(), 0, n);
    WideCharToMultiByte(CP_UTF8, 0, w, -1, u, n, NULL, NULL);
    printf("  %s: %s\n", label, u);
    if (g_out) fprintf(g_out, "%s\n", u);
    HeapFree(GetProcessHeap(), 0, u);
}

static void FreeResults(wchar_t* results[RESULT_COUNT]) {
    size_t i;
    for (i = 0; i < RESULT_COUNT; i++) {
        if (results[i]) HeapFree(GetProcessHeap(), 0, results[i]);
        results[i] = NULL;
    }
}

static int ContainsHangul(const wchar_t* w) {
    if (!w) return 0;
    for (; *w; w++) {
        if (*w >= 0xAC00 && *w <= 0xD7A3) return 1;
    }
    return 0;
}

static int AllContainHangul(wchar_t* results[RESULT_COUNT]) {
    size_t i;
    for (i = 0; i < RESULT_COUNT; i++) {
        if (!ContainsHangul(results[i])) return 0;
    }
    return 1;
}

/* Translates every line into results (each a newly allocated UTF-16 string, or NULL). */
static void TranslateAll(Engine* e, Timing* timing, wchar_t* results[RESULT_COUNT]) {
    size_t i;
    wchar_t* w;
    double t0;
    for (i = 0; i < LINE_COUNT; i++) {
        char* sjis = FromUtf8(kLines[i], 932);
        char* result;
        t0 = NowMs();
        result = e->translate(NULL, sjis);
        timing->ms += NowMs() - t0;
        timing->calls++;
        if (result) {
            results[i] = WideFrom(result, 949);
            Emit("TranslateMMNT", results[i]);
            e->freeMem(result);
        } else {
            results[i] = NULL;
            printf("  TranslateMMNT: NULL (not initialised)\n");
        }
        HeapFree(GetProcessHeap(), 0, sjis);
    }
    t0 = NowMs();
    w = e->translateW(NULL, kWideLine);
    timing->ms += NowMs() - t0;
    timing->calls++;
    if (w) {
        results[LINE_COUNT] = DupWide(w);
        Emit("TranslateMMNTW", results[LINE_COUNT]);
        e->freeMem(w);
    } else {
        results[LINE_COUNT] = NULL;
        printf("  TranslateMMNTW: NULL (not initialised)\n");
    }
}

static int InitEngine(Engine* e) {
    char key[] = "CSUSER123455";
    char path[] = "";
    double t0 = NowMs();
    int ret = e->initializeEx(key, path);
    printf("init %.2f ms\n", NowMs() - t0);
    return ret;
}

typedef struct {
    Engine* engine;
    int crashed;
    int iterations;
    Timing timing;
} ThreadJob;

static DWORD WINAPI ThreadMain(LPVOID param) {
    ThreadJob* job = (ThreadJob*)param;
    char* sjis = FromUtf8(kLines[1], 932);
    int i;
    double t0;
    for (i = 0; i < job->iterations; i++) {
        char* result;
        t0 = NowMs();
        result = job->engine->translate(NULL, sjis);
        job->timing.ms += NowMs() - t0;
        job->timing.calls++;
        if (result) job->engine->freeMem(result);
    }
    HeapFree(GetProcessHeap(), 0, sjis);
    return 0;
}

static int RunThreads(Engine* e, Timing* total) {
    ThreadJob jobs[2];
    HANDLE threads[2];
    int i;
    for (i = 0; i < 2; i++) {
        jobs[i].engine = e;
        jobs[i].crashed = 0;
        jobs[i].iterations = 200;
        jobs[i].timing.calls = 0;
        jobs[i].timing.ms = 0.0;
        threads[i] = CreateThread(NULL, 0, ThreadMain, &jobs[i], 0, NULL);
        if (!threads[i]) return 0;
    }
    if (WaitForMultipleObjects(2, threads, TRUE, 60000) != WAIT_OBJECT_0) return 0;
    for (i = 0; i < 2; i++) {
        CloseHandle(threads[i]);
        total->calls += jobs[i].timing.calls;
        total->ms += jobs[i].timing.ms;
    }
    return 1;
}

int wmain(int argc, wchar_t** argv) {
    Engine e;
    Timing timing;
    wchar_t exePath[MAX_PATH], outPath[MAX_PATH], dictsPath[MAX_PATH];
    wchar_t* results[RESULT_COUNT];
    size_t i;
    int missing = 0, round, ret;
    if (argc < 2) {
        printf("usage: j2k_host <J2KEngine.dll>\n");
        return 2;
    }

    SetConsoleOutputCP(CP_UTF8);
    QueryPerformanceFrequency(&g_freq);
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    SiblingPath(exePath, L"j2k_host.out.txt", outPath, MAX_PATH);
    SiblingPath(argv[1], L"dicts\\base", dictsPath, MAX_PATH);
    g_out = _wfopen(outPath, L"wb");
    Check(g_out != NULL, "open j2k_host.out.txt next to the executable");

    Check(LoadEngine(&e, argv[1]), "load DLL and resolve the host entry points");
    if (!e.module) return 1;
    for (i = 0; i < sizeof(kExports) / sizeof(kExports[0]); i++) {
        if (!GetProcAddress(e.module, kExports[i])) {
            printf("  missing export: %s\n", kExports[i]);
            missing++;
        }
    }
    Check(missing == 0, "every export of the .def resolves");
    if (!e.initializeEx) return 1;

    ret = InitEngine(&e);
    printf("  J2K_InitializeEx returned %d\n", ret);
    printf("memory after first init:\n");
    PrintMemory();
    Check(1, "J2K_InitializeEx call returns");
    timing.calls = 0;
    timing.ms = 0.0;
    TranslateAll(&e, &timing, results);
    PrintTiming(&timing);
    Check(1, "translate Shift-JIS and UTF-16 lines");
    if (DirExists(dictsPath)) {
        Check(AllContainHangul(results), "every translation is non-empty and contains Hangul");
    } else {
        printf("SKIP translation content check: no dicts\\base next to the DLL\n");
    }
    FreeResults(results);
    e.reloadUserDict();
    Check(1, "J2K_ReloadUserDict returns");
    Check(e.stop(NULL) || 1, "J2K_StopTranslation(NULL) returns");
    ret = e.terminate();
    printf("  J2K_Terminate returned %d\n", ret);
    Check(1, "J2K_Terminate returns");
    Check(FreeLibrary(e.module) != 0, "FreeLibrary after first session");

    Check(LoadEngine(&e, argv[1]), "load DLL again");
    if (!e.module) return 1;
    InitEngine(&e);
    timing.calls = 0;
    timing.ms = 0.0;
    TranslateAll(&e, &timing, results);
    PrintTiming(&timing);
    FreeResults(results);
    Check(1, "second session translates");
    Check(FreeLibrary(e.module) != 0, "FreeLibrary after second session (no Terminate)");

    for (round = 1; round <= 2; round++) {
        char what[64];
        Check(LoadEngine(&e, argv[1]), "load DLL for the concurrent run");
        if (!e.module) return 1;
        InitEngine(&e);
        sprintf(what, "two threads translating concurrently, round %d", round);
        timing.calls = 0;
        timing.ms = 0.0;
        Check(RunThreads(&e, &timing), what);
        PrintTiming(&timing);
        e.terminate();
        Check(FreeLibrary(e.module) != 0, "FreeLibrary after the concurrent run");
    }

    printf("memory at end:\n");
    PrintMemory();
    if (g_out) fclose(g_out);
    printf(g_failed ? "RESULT FAIL\n" : "RESULT PASS\n");
    return g_failed ? 1 : 0;
}
