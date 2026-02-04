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

#include "scorpio_utils/threading/thread_streams.hpp"

using scorpio_utils::threading::ThreadSafeIStream;
using scorpio_utils::threading::ThreadSafeIOStream;
using scorpio_utils::threading::ThreadSafeOStream;

std::shared_ptr<ThreadSafeOStream<std::add_lvalue_reference_t<decltype(std::cout)>>> ts_cout =
  ThreadSafeOStream<std::add_lvalue_reference_t<decltype(std::cout)>>::create(std::cout);
std::shared_ptr<ThreadSafeOStream<std::add_lvalue_reference_t<decltype(std::cerr)>>> ts_cerr =
  ThreadSafeOStream<std::add_lvalue_reference_t<decltype(std::cerr)>>::create(std::cerr);
std::shared_ptr<ThreadSafeIStream<std::add_lvalue_reference_t<decltype(std::cin)>>> ts_cin =
  ThreadSafeIStream<std::add_lvalue_reference_t<decltype(std::cin)>>::create(std::cin);
