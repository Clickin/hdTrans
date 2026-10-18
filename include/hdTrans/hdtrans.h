/* C API of the hdtrans engine. Strings are UTF-8. Buffers returned through an out
 * parameter are released with hdTrans_FreeMem. One engine per process. */
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
 * later folders overriding earlier ones. Returns 0, or -1 when no dictionary loads. */
HDTRANS_API int hdTrans_Initialize(const char* dirs);

/* Translates text[0..n) as one line of text. Writes a NUL-terminated UTF-8 buffer to
 * *out and its length (without the NUL) to *outn. Returns 0, or -1 before a
 * successful hdTrans_Initialize. */
HDTRANS_API int hdTrans_Translate(const char* text, size_t n, char** out, size_t* outn);

/* Reloads the dictionary folders of the last hdTrans_Initialize (user dictionary edits). */
HDTRANS_API int hdTrans_Reload(void);

HDTRANS_API void hdTrans_FreeMem(void* p);

/* Version string of the library, e.g. "0.1.0". */
HDTRANS_API const char* hdTrans_Version(void);

#ifdef __cplusplus
}
#endif

#endif
