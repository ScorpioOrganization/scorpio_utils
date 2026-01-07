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
