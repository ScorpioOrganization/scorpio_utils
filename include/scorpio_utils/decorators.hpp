/*
 * scorpio_utils - Scorpio Utility Library for C++
 * Copyright (C) 2026 Igor Zaworski
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#ifdef __has_builtin
#  if __has_builtin(__builtin_expect)
#    define SCU_LIKELY(x)   __builtin_expect(!!(x), 1)
#  endif

#  if __has_builtin(__builtin_expect)
#    define SCU_UNLIKELY(x) __builtin_expect(!!(x), 0)
#  endif

#  if __has_builtin(__builtin_unreachable)
#    define SCU_UNREACHABLE() __builtin_unreachable()
#  endif
#endif

#ifdef __has_attribute
#  if __has_attribute(always_inline)
#    define SCU_ALWAYS_INLINE_RAW __attribute__((always_inline))
#  endif

#  if __has_attribute(noinline)
#    define SCU_NEVER_INLINE __attribute__((noinline))
#  endif

#  if __has_attribute(cold)
#   define SCU_COLD __attribute__((cold))
#  endif


#  if __has_attribute(hot)
#    define SCU_HOT __attribute__((hot))
#  endif

#  if __has_attribute(noreturn)
#    define SCU_NORETURN __attribute__((noreturn))
#  endif

#  if __has_attribute(pure)
#    define SCU_PURE __attribute__((pure))
#  endif

#  if __has_attribute(const)
#    define SCU_CONST_FUNC __attribute__((const))
#  endif
#endif

#ifndef SCU_LIKELY
#  define SCU_LIKELY(x) (!!x)
#endif

#ifndef SCU_UNLIKELY
#  define SCU_UNLIKELY(x) (!!x)
#endif

#ifndef SCU_ALWAYS_INLINE_RAW
#  define SCU_ALWAYS_INLINE_RAW
#endif

#ifndef SCU_ALWAYS_INLINE
#endif

#ifndef SCU_NEVER_INLINE
#  define SCU_NEVER_INLINE
#endif

#ifndef SCU_UNREACHABLE
#  define SCU_UNREACHABLE()
#endif

#ifndef SCU_COLD
#  define SCU_COLD
#endif

#ifndef SCU_HOT
#  define SCU_HOT
#endif

#ifndef SCU_NORETURN
#  define SCU_NORETURN
#endif

#ifndef SCU_PURE
#  define SCU_PURE
#endif

#ifndef SCU_CONST_FUNC
#  define SCU_CONST_FUNC
#endif

#define SCU_ALWAYS_INLINE SCU_ALWAYS_INLINE_RAW inline

#ifdef __COUNTER__
#  define SCU_COUNTER __COUNTER__
#else
#  define SCU_COUNTER __LINE__
#endif

#define SCU_CONCAT_HELPER(a, b) a ## b
#define SCU_CONCAT(a, b) SCU_CONCAT_HELPER(a, b)
#define SCU_UNIQUE_NAME(prefix) SCU_CONCAT(prefix, SCU_COUNTER)

#define SCU_AS(type, x) (static_cast<type>(x))

#if defined(__GNUC__) || defined(_MSC_VER)
#  define SCU_FUNCTION_NAME __PRETTY_FUNCTION__
#else
#  define SCU_FUNCTION_NAME "<unknown>"
#endif

#define SCU_ASSUME(x) \
  do { \
    if (SCU_UNLIKELY(!(x))) { \
      SCU_UNREACHABLE(); \
    } \
  } while(0)

#define SCU_UNCOPYBLE(class_name) \
  class_name(const class_name&) = delete; \
  class_name& operator=(const class_name&) = delete

#define SCU_UNMOVABLE(class_name) \
  class_name(class_name &&) = delete; \
  class_name& operator=(class_name &&) = delete

#define SCU_UNLIKELY_THROW_IF(condition, E, ...) \
  do { \
    if (SCU_UNLIKELY(condition)) { \
      throw E(__VA_ARGS__); \
    } \
  } while(0)
