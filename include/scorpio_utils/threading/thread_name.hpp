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

#ifdef __linux__
#include <pthread.h>
#endif

#include "scorpio_utils/decorators.hpp"

namespace scorpio_utils::threading {
/**
 * Names the calling thread, as shown by `top -H`, `ps -L` and debuggers.
 *
 * \param name At most 15 characters, longer names are left unset by the system.
 */
SCU_ALWAYS_INLINE void set_current_thread_name(const char* name) noexcept {
#ifdef __linux__
  static_cast<void>(pthread_setname_np(pthread_self(), name));
#else
  static_cast<void>(name);
#endif
}
}  // namespace scorpio_utils::threading
