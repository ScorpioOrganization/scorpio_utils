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

#include "scorpio_utils/assert.hpp"
#include "scorpio_utils/compare.hpp"
#include "scorpio_utils/decorators.hpp"
#include "scorpio_utils/defer.hpp"
#include "scorpio_utils/expected.hpp"
#include "scorpio_utils/literals.hpp"
#include "scorpio_utils/misc.hpp"
#include "scorpio_utils/optional_utils.hpp"
#include "scorpio_utils/sat_math.hpp"
#include "scorpio_utils/string_utils.hpp"
#include "scorpio_utils/types.hpp"

using scorpio_utils::literals::operator""_K;
using scorpio_utils::literals::operator""_KB;
using scorpio_utils::literals::operator""_M;
using scorpio_utils::literals::operator""_MB;
using scorpio_utils::literals::operator""_G;
using scorpio_utils::literals::operator""_GB;
using scorpio_utils::literals::operator""_T;
using scorpio_utils::literals::operator""_TB;

namespace scu = scorpio_utils;
