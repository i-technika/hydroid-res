#pragma once

#include "hydroid/res/Export.h"
#include "hydroid/res/VersionNumber.h"

namespace hydroid::res {

// Version of the library that was built, "major.minor.patch". HYDROID_RES_VERSION is the
// version of these headers.
HYDROID_RES_API const char* version();

// Android API level the device Config pretends to be (see the design doc linked from README.md,
// on pinning the device sdkVersion).
inline constexpr int kEmulatedSdkVersion = 34;

}  // namespace hydroid::res
