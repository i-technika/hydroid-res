/* Stable C API over hydroid-res, used by the Swift (UIKit) adapter. */
#ifndef HYDROID_RES_H
#define HYDROID_RES_H

#ifdef __cplusplus
extern "C" {
#endif

/* Library version, "major.minor.patch". The string is static. */
const char* hydroid_res_version(void);

#ifdef __cplusplus
}
#endif

#endif /* HYDROID_RES_H */
