#pragma once

#include <new>

#include "scorpio_utils/decorators.hpp"

namespace scorpio_utils {
template<typename T>
SCU_ALWAYS_INLINE SCU_CONST_FUNC T clone(T v) {
  return v;
}

/**
 * A utility structure to help with visitor overloading.
 * This is useful for creating a visitor that can handle multiple types
 * without needing to define a separate visitor for each type.
 *
 * \tparam T The types to be overloaded.
 */
template<class ... T>
struct VisitorOverloadingHelper : T ... { using T::operator() ...; };
template<class ... T>
VisitorOverloadingHelper(T ...)->VisitorOverloadingHelper<T...>;

#ifdef __cpp_lib_hardware_interference_size
# define SCU_HARDWARE_DESTRUCTIVE_INTERFERENCE_SIZE (std::hardware_destructive_interference_size)
#else
# define SCU_HARDWARE_DESTRUCTIVE_INTERFERENCE_SIZE 64
#endif
}  // namespace scorpio_utils
