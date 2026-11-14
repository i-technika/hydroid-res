/* HYDROID_RES_API marks what a shared hydroid-res exports (#12). C as well as C++, since
 * hydroid_res.h includes it.
 *
 * A static library needs no marks, and that is the default. A consumer of the shared library
 * defines HYDROID_RES_SHARED, which CMake does for whatever links hydroid::res; the library
 * itself also defines HYDROID_RES_BUILDING. */
#ifndef HYDROID_RES_EXPORT_H
#define HYDROID_RES_EXPORT_H

#if !defined(HYDROID_RES_SHARED)
#define HYDROID_RES_API
#elif defined(_WIN32)
#if defined(HYDROID_RES_BUILDING)
#define HYDROID_RES_API __declspec(dllexport)
#else
#define HYDROID_RES_API __declspec(dllimport)
#endif
#elif defined(__GNUC__)
/* The library is built with hidden visibility, so this is all it exports, as on Windows. */
#define HYDROID_RES_API __attribute__((visibility("default")))
#else
#define HYDROID_RES_API
#endif

#endif /* HYDROID_RES_EXPORT_H */
