#pragma once

#include "hydroid/res/Export.h"
#include "hydroid/res/VersionNumber.h"

namespace hydroid::res {

// Version of the library that was built, "major.minor.patch". HYDROID_RES_VERSION is the
// version of these headers.
HYDROID_RES_API const char* version();

// Android API level the device Config pretends to be (see the design doc linked from README.md,
// on pinning the device sdkVersion). Every reference hydroid is checked against uses this level,
// so that -vNN folders and anything else that depends on the SDK compare like for like: the
// golden dump runs on an API 37.0 emulator and the golden app compiles and targets 37, the
// aapt2 golden files come from build-tools 37.0.0 linking against platforms/android-37.0, and
// the @android: framework resources, once bundled (#26), are to come from android-37.0 too
// (golden/aosp still lists an older tree, #49). 37 is the newest level with all of these, and
// 37.0 rather than 37.1 or 37.2 keeps a minor version out of the comparison. Raise them all
// together (SdkBaseline tests).
inline constexpr int kEmulatedSdkVersion = 37;

}  // namespace hydroid::res
