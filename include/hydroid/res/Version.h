#pragma once

namespace hydroid::res {

// Library version, "major.minor.patch".
const char* version();

// Android API level the device Config pretends to be (see the design doc linked from README.md,
// on pinning the device sdkVersion).
inline constexpr int kEmulatedSdkVersion = 34;

}  // namespace hydroid::res
