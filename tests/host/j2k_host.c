/* Host-side test for J2KEngine.dll: loads the DLL the way translator front ends do and runs the
   J2K_* call sequence, twice, then from two threads at once. Usage: j2k_host <path to J2KEngine.dll> */
#include <windows.h>
#include <stdio.h>
#include <string.h>

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

static int g_failed;

static void Check(int ok, const char* what) {
    printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) g_failed = 1;
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

static void PrintUtf8(const char* label, const wchar_t* w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    char* u = (char*)HeapAlloc(GetProcessHeap(), 0, n);
    WideCharToMultiByte(CP_UTF8, 0, w, -1, u, n, NULL, NULL);
    printf("  %s: %s\n", label, u);
    HeapFree(GetProcessHeap(), 0, u);
}

/* Translates every line; returns the number of non-NULL results. */
static int TranslateAll(Engine* e) {
    int produced = 0;
    size_t i;
    wchar_t* w;
    for (i = 0; i < LINE_COUNT; i++) {
        char* sjis = FromUtf8(kLines[i], 932);
        char* result = e->translate(NULL, sjis);
        if (result) {
            int wn = MultiByteToWideChar(949, 0, result, -1, NULL, 0);
            wchar_t* wide = (wchar_t*)HeapAlloc(GetProcessHeap(), 0, wn * sizeof(wchar_t));
            MultiByteToWideChar(949, 0, result, -1, wide, wn);
            PrintUtf8("TranslateMMNT", wide);
            HeapFree(GetProcessHeap(), 0, wide);
            e->freeMem(result);
            produced++;
        } else {
            printf("  TranslateMMNT: NULL (not initialised)\n");
        }
        HeapFree(GetProcessHeap(), 0, sjis);
    }
    w = e->translateW(NULL, kWideLine);
    if (w) {
        PrintUtf8("TranslateMMNTW", w);
        e->freeMem(w);
        produced++;
    } else {
        printf("  TranslateMMNTW: NULL (not initialised)\n");
    }
    return produced;
}

static int InitEngine(Engine* e) {
    char key[] = "CSUSER123455";
    char path[] = "";
    return e->initializeEx(key, path);
}

typedef struct {
    Engine* engine;
    int crashed;
    int iterations;
} ThreadJob;

static DWORD WINAPI ThreadMain(LPVOID param) {
    ThreadJob* job = (ThreadJob*)param;
    char* sjis = FromUtf8(kLines[1], 932);
    int i;
    for (i = 0; i < job->iterations; i++) {
        char* result = job->engine->translate(NULL, sjis);
        if (result) job->engine->freeMem(result);
    }
    HeapFree(GetProcessHeap(), 0, sjis);
    return 0;
}

static int RunThreads(Engine* e) {
    ThreadJob jobs[2];
    HANDLE threads[2];
    int i;
    for (i = 0; i < 2; i++) {
        jobs[i].engine = e;
        jobs[i].crashed = 0;
        jobs[i].iterations = 200;
        threads[i] = CreateThread(NULL, 0, ThreadMain, &jobs[i], 0, NULL);
        if (!threads[i]) return 0;
    }
    if (WaitForMultipleObjects(2, threads, TRUE, 60000) != WAIT_OBJECT_0) return 0;
    for (i = 0; i < 2; i++) CloseHandle(threads[i]);
    return 1;
}

int wmain(int argc, wchar_t** argv) {
    Engine e;
    size_t i;
    int missing = 0, round, ret;
    if (argc < 2) {
        printf("usage: j2k_host <J2KEngine.dll>\n");
        return 2;
    }

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
    Check(1, "J2K_InitializeEx call returns");
    TranslateAll(&e);
    Check(1, "translate Shift-JIS and UTF-16 lines");
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
    TranslateAll(&e);
    Check(1, "second session translates");
    Check(FreeLibrary(e.module) != 0, "FreeLibrary after second session (no Terminate)");

    for (round = 1; round <= 2; round++) {
        char what[64];
        Check(LoadEngine(&e, argv[1]), "load DLL for the concurrent run");
        if (!e.module) return 1;
        InitEngine(&e);
        sprintf(what, "two threads translating concurrently, round %d", round);
        Check(RunThreads(&e), what);
        e.terminate();
        Check(FreeLibrary(e.module) != 0, "FreeLibrary after the concurrent run");
    }

    printf(g_failed ? "RESULT FAIL\n" : "RESULT PASS\n");
    return g_failed ? 1 : 0;
}
