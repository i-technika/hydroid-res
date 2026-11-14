#pragma once

namespace hydroid::res {

// Library version, "major.minor.patch".
const char* version();

// Android API level the device Config pretends to be (see "기기 sdkVersion 고정").
inline constexpr int kEmulatedSdkVersion = 34;

}  // namespace hydroid::res
