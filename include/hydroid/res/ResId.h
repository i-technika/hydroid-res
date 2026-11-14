#pragma once

#include <cstdint>

namespace hydroid::res {

using ResId = uint32_t;  // 0xPPTTEEEE, same layout as Android

inline constexpr uint8_t kFrameworkPackageId = 0x01;
inline constexpr uint8_t kAppPackageId = 0x7f;

constexpr ResId makeResId(uint8_t package, uint8_t type, uint16_t entry) {
  return (ResId{package} << 24) | (ResId{type} << 16) | entry;
}

constexpr uint8_t packageId(ResId id) { return static_cast<uint8_t>(id >> 24); }
constexpr uint8_t typeId(ResId id) { return static_cast<uint8_t>(id >> 16); }
constexpr uint16_t entryId(ResId id) { return static_cast<uint16_t>(id); }

}  // namespace hydroid::res
