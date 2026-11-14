#pragma once

#include <cstdint>
#include <optional>

#include "hydroid/res/Value.h"

// Android's "complex" encoding of dimensions and fractions (Res_value data of TYPE_DIMENSION and
// TYPE_FRACTION): a 24-bit signed mantissa, a 2-bit radix saying where its binary point is, and a
// 4-bit unit.
//
//   31            8   5  4 3    0
//   [  mantissa   ][..][rx][unit]

namespace hydroid::res {

constexpr uint32_t kComplexUnitShift = 0;
constexpr uint32_t kComplexUnitMask = 0xf;
constexpr uint32_t kComplexRadixShift = 4;
constexpr uint32_t kComplexRadixMask = 0x3;
constexpr uint32_t kComplexMantissaShift = 8;
constexpr uint32_t kComplexMantissaMask = 0xffffff;

// Radix values: integer bits . fraction bits of the mantissa.
constexpr uint32_t kComplexRadix23p0 = 0;
constexpr uint32_t kComplexRadix16p7 = 1;
constexpr uint32_t kComplexRadix8p15 = 2;
constexpr uint32_t kComplexRadix0p23 = 3;

// Units of TYPE_FRACTION; TYPE_DIMENSION units are DimUnit.
constexpr uint32_t kComplexUnitFraction = 0;        // "%"
constexpr uint32_t kComplexUnitFractionParent = 1;  // "%p"

// Encodes `value` (already scaled: "50%" is 0.5) with `unit`, bit for bit as aapt2's
// ResTable::stringToFloat does, including its precision loss and the mantissa wrapping around
// past +/-2^23 (9000000dp reads back as -7777216). nullopt where aapt2 is undefined: non-finite
// values and |value| >= 2^41.
std::optional<uint32_t> floatToComplex(float value, uint32_t unit);

// TypedValue.complexToFloat: the number without its unit.
float complexToFloat(uint32_t data);

constexpr uint32_t complexUnit(uint32_t data) {
  return (data >> kComplexUnitShift) & kComplexUnitMask;
}

// The subset of android.util.DisplayMetrics that unit conversion reads.
struct DisplayMetrics {
  float density;        // dp -> px
  float scaledDensity;  // sp -> px
  float xdpi;           // pt, in, mm -> px

  // As ResourcesImpl derives them from Configuration, where a fontScale of 0 (unset) means 1.
  // Android takes xdpi from the physical display, so it defaults to densityDpi here; set the
  // real one if pt/in/mm matter.
  static DisplayMetrics forDensity(uint32_t densityDpi, float fontScale = 1.0f) {
    const float scale = static_cast<float>(densityDpi) * (1.0f / 160);  // DENSITY_DEFAULT_SCALE
    return {scale, scale * (fontScale != 0 ? fontScale : 1.0f), static_cast<float>(densityDpi)};
  }
};

// TypedValue.applyDimension. Linear sp only: Android 14+ scales sp non-linearly from fontScale
// 1.03 up.
float applyDimension(DimUnit unit, float value, const DisplayMetrics& metrics);

// TypedValue.complexToDimension (what Resources.getDimension returns). 0 for an unknown unit.
float complexToDimension(uint32_t data, const DisplayMetrics& metrics);

// TypedValue.complexToFraction: "%" scales `base`, "%p" scales `parentBase`, else 0.
float complexToFraction(uint32_t data, float base, float parentBase);

}  // namespace hydroid::res
