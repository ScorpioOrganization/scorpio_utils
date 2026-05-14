# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

`scorpio_utils` is a ROS2 (humble/jazzy) C++17 utility library built with `ament_cmake`. It is a header-heavy collection of independent libraries (networking, threading primitives, geometry, GPS conversion, time handling, ROS helpers, testing aids) that other packages in the workspace depend on. Most of the public API lives in [include/scorpio_utils/](include/scorpio_utils/); the `src/` tree only holds out-of-line implementations.

Treat the package as a library — there are no executables, only shared libraries listed in `LIBRARY_LIST` in [CMakeLists.txt](CMakeLists.txt).

## Build, test, lint

Builds expect to live in a colcon workspace rooted at `/home/igor/ros2_ws` (one level up from `src/core/scorpio_utils`). Run all colcon commands from the workspace root, not from this package.

```bash
# Build (from workspace root)
colcon build --packages-select scorpio_utils --symlink-install

# Run all tests
colcon test --packages-select scorpio_utils --return-code-on-test-failure
colcon test-result --verbose

# Run a single test binary directly after build
./build/scorpio_utils/<test_name>           # e.g. ./build/scorpio_utils/test_channel
./build/scorpio_utils/<test_name> --gtest_filter='Suite.Case'
```

Compiler flags are strict: `-std=c++17 -Wall -Wextra -Wpedantic -pedantic-errors -Wconversion -Wsign-conversion -Werror`. Any new warning fails the build — fix the root cause instead of suppressing.

Linting (mirrors `.github/workflows/ci-core.yml`; CI gates merges to `master`):

```bash
# C++ style — cpplint config in CPPLINT.cfg (line length 120, copyright check disabled)
find . \( -name "*.cpp" -o -name "*.hpp" \) | xargs ament_cpplint

# C++ formatting — uncrustify against the in-repo config
find . \( -name "*.cpp" -o -name "*.hpp" \) | xargs ament_uncrustify -c uncrustify.cfg

# Other linters CI runs
find . -name CMakeLists.txt | xargs ament_lint_cmake
find . -name "*.xml"        | xargs ament_xmllint
```

VSCode is configured to format C++ on save with the `zachflower.uncrustify` extension using the repo's `uncrustify.cfg`.

## Architecture

### Naming, macros, headers

- All public symbols live in `namespace scorpio_utils` (sub-namespaces by module: `::network`, `::threading`, `::geometry`, `::ros`, `::time_provider`, `::time_utils`, `::testing`, `::logger`, `::gps`, `::literals`).
- [scorpio_utils/prelude.hpp](include/scorpio_utils/prelude.hpp) is the catch-all "bring everything common" header; including it also pulls in size literals (`_K`, `_KB`, `_M`, etc.) and aliases the namespace as `scu`.
- Every macro is prefixed `SCU_`. Common ones:
  - [decorators.hpp](include/scorpio_utils/decorators.hpp): `SCU_ALWAYS_INLINE`, `SCU_LIKELY` / `SCU_UNLIKELY`, `SCU_HOT`, `SCU_COLD`, `SCU_UNREACHABLE`, `SCU_NORETURN`, `SCU_PURE`, `SCU_CONST_FUNC`, `SCU_UNCOPYBLE`, `SCU_UNMOVABLE`, `SCU_AS(T, x)`, `SCU_UNIQUE_NAME`.
  - [assert.hpp](include/scorpio_utils/assert.hpp): `SCU_ASSERT(cond, msg)`, `SCU_DO_AND_ASSERT`, `SCU_UNIMPLEMENTED()`. Asserts call `std::terminate()`; can be compiled out with `-DSCU_NO_ASSERT`.
  - [logger/logger.hpp](include/scorpio_utils/logger/logger.hpp): `SCU_LOG_{FATAL,ERROR,WARNING,INFO,DEBUG,TRACE}(loggerPtr, fmt, args...)`. Compile-time gated by `SCU_LOG_LEVEL` (default 3 = INFO; the `network` library is built with `SCU_LOG_LEVEL=5`). All formatting goes through `fmt::format`; enums are pretty-printed via `magic_enum`.
- Error handling uses the custom [`Expected<T, E>`](include/scorpio_utils/expected.hpp) (variant-based) and `Unexpected<E>`. There is no `std::expected` dependency — do not reach for the standard one.
- `Success` and `Empty` (in [types.hpp](include/scorpio_utils/types.hpp)) are the sentinel value types for `Expected<Success, std::string>`-style returns; `Impossible` is the uninhabited "this branch cannot happen" type.

### Build-flag-driven test variants of the network stack

The network library has a single source-of-truth (`src/network/scorpio_udp.cpp`, `include/scorpio_utils/network/{scorpio_udp,udp}.hpp`) that compiles into **four** different artifacts driven by preprocessor flags. This is intentional — when editing UDP or ScorpioUdp code, make sure all four still build and pass.

| Target | Defines set in [CMakeLists.txt](CMakeLists.txt) | What `UdpSocket` becomes |
|---|---|---|
| `network` (production) | `SCU_LOG_LEVEL=5` | Real BSD socket implementation |
| `network_mock` → `test_mock_udp`, `test_scorpio_udp_mock` | `SCU_MOCK_UDP_DELAY_{MIN,MAX}_MS`, `SCU_MOCK_UDP_PACKET_LOSS_PERCENTAGE` | In-process mock with simulated delay/loss (see [test/network/mock_udp.cpp](test/network/mock_udp.cpp)) |
| `network_gmock` → `test_scorpio_udp_gmock` | `SCORPIO_UTILS_UDP_GMOCK=1` | `UdpSocket` becomes a gmock object (`MOCK_METHOD` per call); compiled with `-std=gnu++17` because gmock needs it |
| `network_framework_test` → `test_scorpio_udp_framework` | `SCORPIO_UTILS_FRAMEWORK=1` | `UdpSocket` exposes `_send_queue` / `_receive_queue` channels so tests script socket traffic |

`SCORPIO_UTILS_UDP_GMOCK` or `SCORPIO_UTILS_FRAMEWORK` also activates `SCU_UDP_MOCK` inside [network/scorpio_udp.hpp](include/scorpio_utils/network/scorpio_udp.hpp), which swaps the production `LazyTimeProvider` for `MockTimeProvider`. Time-dependent logic (heartbeats, timeouts, retries) is therefore driven by test code in mock builds.

Compile-time tunables that govern ScorpioUDP behavior (override via `target_compile_definitions` if needed):

```
SCU_UDP_MAX_PACKET_SIZE              = 512
SCU_UDP_QOS_DEPTH_SAFETY_BUFFER      = 2048
SCU_UDP_UNRELIABLE_DATA_EXPIRY_NS    = 500'000'000
SCU_UDP_HEARTBEAT_PERIOD             = 50'000'000  (ns)
SCU_UDP_TIMEOUT                      = 5'000'000'000
SCU_UDP_CREATE_RETRY_PERIOD          = 5'000'000'000
SCU_UDP_DEBUG_LOG_ENABLED            = 0
```

### Threading primitives

[threading/](include/scorpio_utils/threading/) is the largest module. Notable pieces:

- `Channel<T, Capacity>` ([channel.hpp](include/scorpio_utils/threading/channel.hpp)) — fixed-capacity MPMC queue, throws `ClosedChannelException` on send/receive after close. `Capacity` is a compile-time constant; the network framework variant uses `1024 * 1024`.
- `EagerSelect` ([eager_select.hpp](include/scorpio_utils/threading/eager_select.hpp)) — `select`-style multiplexer over anything exposing `SCU_EAGER_SELECT_IS_READY()` / `SCU_EAGER_SELECT_GET_VALUE()`. Channels and `ScorpioUdpStream` both implement this protocol so they can be waited on uniformly.
- `Signal`, `WaitGroup`, `AndThen`, `Future`, `JThread`, `ThreadPool`, `ThreadStreams`, `Waitable` round out the rest.

When adding a type that should compose with `EagerSelect`, implement both `SCU_EAGER_SELECT_IS_READY` and `SCU_EAGER_SELECT_GET_VALUE` and check the trait check in [eager_select.hpp](include/scorpio_utils/threading/eager_select.hpp) passes.

### Time providers and dependency injection

Code that reads "now" takes a `time_provider::TimeProvider*` rather than calling `std::chrono` directly. Production code uses [`LazyTimeProvider`](include/scorpio_utils/time_provider/lazy_time_provider.hpp); tests inject [`MockTimeProvider`](include/scorpio_utils/testing/mock_time_provider.hpp) to drive simulated clocks. Don't bypass this — the ScorpioUDP test variants depend on it for deterministic time travel.

### ROS-specific helpers

[ros/](include/scorpio_utils/ros/) is the only directory that links `rclcpp` into the public API. `RosVariable<T, IgnoreSameValue>` ([ros_variable.hpp](include/scorpio_utils/ros/ros_variable.hpp)) wraps a publisher/subscriber pair around a single value type. The non-ROS modules (`threading`, `network`, `geometry`, …) do not include `rclcpp` and should stay that way — keep the dependency contained.

## Conventions

- C++17 only. No `std::expected`, `std::format`, structured bindings in `if`, etc. — use `Expected` and `fmt::format` instead.
- Prefer header-only templates; put non-template definitions in `src/<module>/`.
- Public headers must be self-contained and use angle-quoted absolute includes (`#include "scorpio_utils/foo.hpp"`).
- License header (GPL-3.0-or-later block in [prelude.hpp](include/scorpio_utils/prelude.hpp)) appears at the top of source files but is *disabled* in cpplint config — don't add the cpplint copyright check back.
- Architecture is detected at CMake time and exposed as `ARCH_X64` / `ARCH_ARM` / `ARCH_UNKNOWN` defines. Use these rather than re-detecting in code.
