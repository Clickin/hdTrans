/* C API of the hdtrans engine. Strings are UTF-8. Buffers returned through an out
 * parameter are released with hdTrans_FreeMem. One engine and one session per process:
 * names and context found in earlier calls shape later translations.
 * Calls are thread-safe, not reentrant; finish all calls before unloading the library. */
#ifndef HDTRANS_H
#define HDTRANS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32) && defined(HDTRANS_BUILD_DLL)
#define HDTRANS_API __declspec(dllexport)
#elif defined(_WIN32) && defined(HDTRANS_USE_DLL)
#define HDTRANS_API __declspec(dllimport)
#else
#define HDTRANS_API
#endif

/* Loads the dictionary folders listed in dirs (NUL-separated, double NUL at the end),
 * later folders overriding earlier ones, then userdict.tsv next to the library when
 * present; a userdict.tsv that does not load is left out (hdTrans_UserDictError).
 * Translations check userdict.tsv at most once a second and reload it in the background
 * when it changes. Returns 0, or -1 when no dictionary loads or a concurrent Terminate,
 * successful Initialize or successful Reload cancels this load. */
HDTRANS_API int hdTrans_Initialize(const char* dirs);

/* Releases the session and dictionaries. Repeated calls are harmless; Initialize may be called again. */
HDTRANS_API void hdTrans_Terminate(void);

/* Translates text[0..n), one or more "\n"-separated lines, in the process session.
 * Writes a NUL-terminated UTF-8 buffer to *out and its length (without the NUL) to
 * *outn. Returns 0, or -1 when uninitialized or terminated. */
HDTRANS_API int hdTrans_Translate(const char* text, size_t n, char** out, size_t* outn);

/* Reloads the dictionary folders and userdict.tsv. Returns 0, or -1 when
 * uninitialized, terminated, or the reload fails; a userdict.tsv that does not load
 * fails it and keeps the dictionaries loaded before. */
HDTRANS_API int hdTrans_Reload(void);

/* Writes why the last version of userdict.tsv read did not load, one line per error, as
 * a buffer like hdTrans_Translate does; empty when it loaded or there is none. Returns 0,
 * or -1 when uninitialized or terminated. */
HDTRANS_API int hdTrans_UserDictError(char** out, size_t* outn);

HDTRANS_API void hdTrans_FreeMem(void* p);

/* Version string of the library, e.g. "0.1.0". */
HDTRANS_API const char* hdTrans_Version(void);

#ifdef __cplusplus
}
#endif

#endif
