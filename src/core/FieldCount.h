#pragma once

#include <cstddef>

// The number of fields of an aggregate, counted by aggregate initialization, so that a check on a
// struct's fields needs no number written down (#52). A C array counts as one field per element,
// as brace elision spreads AnyFields over it; std::array counts as one.

namespace hydroid::res::detail {

// Converts to the type of any field, so that T{AnyField{}, ...} takes as many of them as the
// aggregate T has fields. Only ever named in decltype.
struct AnyField {
  template <typename T>
  operator T() const;
};

template <typename T, typename... F>
constexpr auto takes(int) -> decltype(T{F{}...}, true) {
  return true;
}
template <typename T, typename... F>
constexpr bool takes(...) {
  return false;
}

template <typename T, typename... F>
constexpr size_t fieldCount() {
  if constexpr (takes<T, F..., AnyField>(0)) {
    return fieldCount<T, F..., AnyField>();
  } else {
    return sizeof...(F);
  }
}

}  // namespace hydroid::res::detail
