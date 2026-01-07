#pragma once

#include <type_traits>

namespace scorpio_utils::ros {
template<typename, typename = std::void_t<>>
struct IsMessageType : std::false_type { };
template<typename T>
struct IsMessageType<T, std::void_t<typename T::template ConstUniquePtrWithDeleter<>>>: std::true_type { };
}  // namespace scorpio_utils::ros
