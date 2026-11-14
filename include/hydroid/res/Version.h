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
// golden dump runs on an API 36 emulator and the golden app compiles and targets 36, the aapt2
// golden files come from build-tools 36.1.0 linking against platforms/android-36, and the
// bundled @android: framework resources are those of android-36. The golden data was already
// made at 36, so picking it remade none of it. Raise them all together (SdkBaseline tests).
inline constexpr int kEmulatedSdkVersion = 36;

}  // namespace hydroid::res
