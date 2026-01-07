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
