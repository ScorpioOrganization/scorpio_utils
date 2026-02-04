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

#include <exception>
#include <iostream>
#include "scorpio_utils/decorators.hpp"

#ifndef SCU_ASSERT_TERMINATE
# define SCU_ASSERT_TERMINATE \
  do { \
    std::terminate(); \
    SCU_UNREACHABLE(); \
  } while(0)
#endif

#ifndef SCU_NO_ASSERT
# define SCU_ASSERT(condition, message) \
  do { \
    if (SCU_UNLIKELY(!(condition))) { \
      std::cout << std::endl; \
      std::cerr << "Assertion failed in: " << SCU_FUNCTION_NAME << '\n' \
                << __FILE__ << ":" << __LINE__ << ": " << message << '\n' \
                << #condition << std::endl; \
      SCU_ASSERT_TERMINATE; \
    } \
  } while(0)

# define SCU_DO_AND_ASSERT(condition, message) SCU_ASSERT(condition, message)

#else
# define SCU_ASSERT(condition, message) do { } while(0)

# define SCU_DO_AND_ASSERT(condition, message) \
  do { \
    condition; \
  } while(0)
#endif

#define SCU_UNIMPLEMENTED() \
  do { \
    std::cout << std::endl; \
    std::cerr << "Unimplemented code reached in: " << SCU_FUNCTION_NAME << '\n' \
              << __FILE__ << ":" << __LINE__ << std::endl; \
    SCU_ASSERT_TERMINATE; \
  } while(0)
