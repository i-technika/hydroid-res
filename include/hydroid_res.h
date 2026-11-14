/* Stable C API over hydroid-res, used by the Swift (UIKit) adapter. */
#ifndef HYDROID_RES_H
#define HYDROID_RES_H

#include "hydroid/res/Export.h"
#include "hydroid/res/VersionNumber.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Version of the library that was built, "major.minor.patch". The string is static.
 * HYDROID_RES_VERSION is the version of these headers. */
HYDROID_RES_API const char* hydroid_res_version(void);

#ifdef __cplusplus
}
#endif

#endif /* HYDROID_RES_H */
