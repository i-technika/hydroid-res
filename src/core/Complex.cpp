#include "hydroid/res/Complex.h"

#include <cmath>
#include <cstdint>

namespace hydroid::res {

std::optional<uint32_t> floatToComplex(float value, uint32_t unit) {
  // Mirrors the unit branch of ResTable::stringToFloat. Every step stays in float like the
  // original: `f * (1 << 23) + .5f` rounds to 24 bits before the integer cast.
  if (!std::isfinite(value)) return std::nullopt;
  const bool neg = value < 0;
  if (neg) value = -value;
  const float scaled = value * static_cast<float>(1 << 23) + .5f;
  if (scaled >= 18446744073709551616.0f) return std::nullopt;  // 2^64: the cast would be UB
  const auto bits = static_cast<uint64_t>(scaled);

  uint32_t radix;
  uint32_t shift;
  if ((bits & 0x7fffff) == 0) {
    // Always 23p0 when there is no fraction, to keep things readable.
    radix = kComplexRadix23p0;
    shift = 23;
  } else if ((bits & 0xffffffffff800000ull) == 0) {
    radix = kComplexRadix0p23;
    shift = 0;
  } else if ((bits & 0xffffffff80000000ull) == 0) {
    radix = kComplexRadix8p15;
    shift = 8;
  } else if ((bits & 0xffffff8000000000ull) == 0) {
    radix = kComplexRadix16p7;
    shift = 16;
  } else {
    // Needs the whole range, so no fraction. Past 2^23 the mantissa wraps like aapt2's.
    radix = kComplexRadix23p0;
    shift = 23;
  }
  auto mantissa = static_cast<uint32_t>((bits >> shift) & kComplexMantissaMask);
  if (neg) mantissa = (0u - mantissa) & kComplexMantissaMask;
  return ((unit & kComplexUnitMask) << kComplexUnitShift) | (radix << kComplexRadixShift) |
         (mantissa << kComplexMantissaShift);
}

float complexToFloat(uint32_t data) {
  // 2^0, 2^-7, 2^-15, 2^-23: the fraction bits of each radix. The products are exact.
  constexpr float kRadixMults[] = {1.0f, 1.0f / (1 << 7), 1.0f / (1 << 15), 1.0f / (1 << 23)};
  auto mantissa = static_cast<int32_t>((data >> kComplexMantissaShift) & kComplexMantissaMask);
  if (mantissa & 0x800000) mantissa -= 0x1000000;  // sign-extend 24 bits
  return static_cast<float>(mantissa) *
         kRadixMults[(data >> kComplexRadixShift) & kComplexRadixMask];
}

float applyDimension(DimUnit unit, float value, const DisplayMetrics& metrics) {
  switch (unit) {
    case DimUnit::Px: return value;
    case DimUnit::Dp: return value * metrics.density;
    case DimUnit::Sp: return value * metrics.scaledDensity;
    case DimUnit::Pt: return value * metrics.xdpi * (1.0f / 72);
    case DimUnit::In: return value * metrics.xdpi;
    case DimUnit::Mm: return value * metrics.xdpi * (1.0f / 25.4f);
  }
  return 0;
}

float complexToDimension(uint32_t data, const DisplayMetrics& metrics) {
  const uint32_t unit = complexUnit(data);
  if (unit > static_cast<uint32_t>(DimUnit::Mm)) return 0;
  return applyDimension(static_cast<DimUnit>(unit), complexToFloat(data), metrics);
}

float complexToFraction(uint32_t data, float base, float parentBase) {
  switch (complexUnit(data)) {
    case kComplexUnitFraction: return complexToFloat(data) * base;
    case kComplexUnitFractionParent: return complexToFloat(data) * parentBase;
    default: return 0;
  }
}

}  // namespace hydroid::res
