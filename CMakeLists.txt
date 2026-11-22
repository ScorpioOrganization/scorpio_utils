cmake_minimum_required(VERSION 3.8)
project(scorpio_utils)

find_package(ament_cmake REQUIRED)
find_package(backward_ros REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)
find_package(fmt REQUIRED)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64")
    message(STATUS "Detected architecture: x64")
    add_definitions(-DARCH_X64)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64" OR CMAKE_SYSTEM_PROCESSOR MATCHES "arm64")
    message(STATUS "Detected architecture: ARM")
    add_definitions(-DARCH_ARM)
else()
    message(WARNING "Unknown architecture: ${CMAKE_SYSTEM_PROCESSOR}")
    add_definitions(-DARCH_UNKNOWN)
endif()

if(DOCKER_BUILD)
  add_definitions(-DDOCKER_BUILD)
endif()

add_compile_options(-std=c++17 -Wall -Wextra -Wpedantic -pedantic-errors -Wdeprecated
  -Wc++17-compat -Wconversion -Wsign-conversion -Wpessimizing-move -Werror
  -g -fno-omit-frame-pointer
)

include_directories(
  include/
  # itertools/include/
)

add_library(threading SHARED
  src/threading/signal.cpp
  src/threading/thread_pool.cpp
  src/threading/thread_streams.cpp
  src/threading/waitable.cpp
)

add_library(time_utils SHARED
  src/time_utils/time_string.cpp
)

add_library(time_provider SHARED
  src/time_provider/lazy_time_provider.cpp
)

add_library(gps SHARED
  src/gps/localization_converter.cpp
)

add_library(lifetime_helper
  src/testing/lifetime_helper.cpp
)

add_library(buffered_file_saver SHARED
  src/buffered_file_saver.cpp
)

add_library(string_utils SHARED
  src/string_utils.cpp
)

add_library(network SHARED
  src/network/ip.cpp
  src/network/scorpio_udp.cpp
  src/network/udp.cpp
)
target_link_libraries(network fmt::fmt)

target_compile_definitions(network PRIVATE
  -DSCU_LOG_LEVEL=5
)

set(LIBRARY_LIST
  buffered_file_saver
  gps
  lifetime_helper
  network
  string_utils
  time_provider
  time_utils
  threading
)

foreach(LIBRARY ${LIBRARY_LIST})
  ament_export_libraries(${LIBRARY})
endforeach()


install(TARGETS
  ${LIBRARY_LIST}
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  INCLUDES DESTINATION include
)

install(DIRECTORY
  include/
  # itertools/include/
  DESTINATION include
)

if(BUILD_TESTING)
  add_compile_options(-g)
  find_package(ament_cmake_gtest REQUIRED)
  find_package(ament_cmake_gmock REQUIRED)
  ament_find_gmock()

  # Suppress warnings from gmock
  target_compile_options(${GMOCK_MAIN_LIBRARIES} PRIVATE -Wno-deprecated -Wno-sign-conversion)
  target_compile_options(${GMOCK_LIBRARIES} PRIVATE -Wno-deprecated -Wno-sign-conversion)

  # Every test and everything it tests is built with AddressSanitizer and UndefinedBehaviorSanitizer, and any report
  # fails the test (-fno-sanitize-recover: UBSan aborts instead of printing and going on). The installed libraries
  # stay uninstrumented, because a process that loads an ASan library must have the ASan runtime loaded first: the
  # tests link sanitized copies of them instead, built from the same sources (<library>_sanitized).
  set(SCU_SANITIZER_FLAGS -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
  # GCC documents that the sanitizers raise false -Wmaybe-uninitialized positives (GCC 13 reports one in
  # std::variant<int, double, std::string>); the uninstrumented library builds keep the warning
  set(SCU_SANITIZER_COMPILE_FLAGS ${SCU_SANITIZER_FLAGS})
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    list(APPEND SCU_SANITIZER_COMPILE_FLAGS -Wno-maybe-uninitialized)
  endif()
  function(scu_sanitize TARGET)
    target_compile_options(${TARGET} PRIVATE ${SCU_SANITIZER_COMPILE_FLAGS})
    target_link_options(${TARGET} PUBLIC ${SCU_SANITIZER_FLAGS})
  endfunction()
  # Test executables also print the stack of a UBSan report
  function(scu_sanitize_test TEST_NAME)
    scu_sanitize(${TEST_NAME})
    set_property(TEST ${TEST_NAME} APPEND PROPERTY ENVIRONMENT "UBSAN_OPTIONS=print_stacktrace=1")
  endfunction()

  set(SANITIZED_LIBRARY_LIST)
  foreach(LIBRARY ${LIBRARY_LIST})
    get_target_property(LIBRARY_TYPE ${LIBRARY} TYPE)
    string(REPLACE "_LIBRARY" "" LIBRARY_TYPE ${LIBRARY_TYPE})
    get_target_property(LIBRARY_SOURCES ${LIBRARY} SOURCES)
    add_library(${LIBRARY}_sanitized ${LIBRARY_TYPE} ${LIBRARY_SOURCES})
    get_target_property(LIBRARY_DEFINITIONS ${LIBRARY} COMPILE_DEFINITIONS)
    if(LIBRARY_DEFINITIONS)
      target_compile_definitions(${LIBRARY}_sanitized PRIVATE ${LIBRARY_DEFINITIONS})
    endif()
    get_target_property(LIBRARY_LINK_LIBRARIES ${LIBRARY} LINK_LIBRARIES)
    if(LIBRARY_LINK_LIBRARIES)
      target_link_libraries(${LIBRARY}_sanitized ${LIBRARY_LINK_LIBRARIES})
    endif()
    scu_sanitize(${LIBRARY}_sanitized)
    list(APPEND SANITIZED_LIBRARY_LIST ${LIBRARY}_sanitized)
  endforeach()

  # What the ScorpioUdp variant tests link next to their own network library
  set(SANITIZED_SUPPORT_LIBRARIES
    buffered_file_saver_sanitized
    gps_sanitized
    lifetime_helper_sanitized
    string_utils_sanitized
    time_provider_sanitized
    time_utils_sanitized
    threading_sanitized
  )

  # List of test names
  set(TESTS_NAMES
    geometry/test_discrete_rotation.cpp
    geometry/test_two_sided_vector.cpp
    geometry/test_point.cpp
    geometry/test_plane.cpp
    geometry/test_position.cpp
    geometry/test_quaternion.cpp
    geometry/test_utils.cpp
    gps/test_localization_converter.cpp
    network/test_orderer.cpp
    network/test_scorpio_udp.cpp
    network/test_types.cpp
    network/test_udp.cpp
    ros/test_ros_utils.cpp
    ros/test_ros_variable.cpp
    testing/test_lifetime_helper.cpp
    threading/test_and_then.cpp
    threading/test_channel.cpp
    threading/test_eager_select.cpp
    threading/test_jthread.cpp
    threading/test_signal.cpp
    threading/test_thread_pool.cpp
    threading/test_thread_streams.cpp
    threading/test_wait_group.cpp
    time_provider/test_lazy_time_provider.cpp
    time_provider/test_steady_time_provider.cpp
    time_provider/test_system_time_provider.cpp
    time_utils/test_time_string.cpp
    test_buffered_file_saver.cpp
    test_compare.cpp
    test_defer.cpp
    test_endianness.cpp
    test_expected.cpp
    test_misc.cpp
    test_optional_utils.cpp
    test_sat_math.cpp
    test_stack_shared_ptr.cpp
    test_stream_splitter.cpp
    test_string_utils.cpp
    test_unique_function.cpp
    test_visitor_overloading_helper.cpp
  )

  # Add all tests using the macro
  foreach(PATH_TO_TEST ${TESTS_NAMES})
    get_filename_component(TEST_NAME ${PATH_TO_TEST} NAME_WE)
    ament_add_gtest(${TEST_NAME}
      test/${PATH_TO_TEST})
    target_link_libraries(${TEST_NAME} ${SANITIZED_LIBRARY_LIST})
    ament_target_dependencies(${TEST_NAME} rclcpp std_msgs)
    scu_sanitize_test(${TEST_NAME})
  endforeach()

  # Real-socket integration suite runs 6 sleep-dominated cases sequentially (~60s),
  # right on the ament_add_gtest 60s default. Give it a generous budget.
  set_tests_properties(test_scorpio_udp PROPERTIES TIMEOUT 180)

  add_library(network_mock STATIC
    test/network/mock_udp.cpp
    src/network/ip.cpp
    src/network/scorpio_udp.cpp
  )
  target_link_libraries(network_mock fmt::fmt)
  scu_sanitize(network_mock)

  # Test of mock_udp.cpp required for network tests
  ament_add_gtest(test_mock_udp test/network/test_udp.cpp)

  target_compile_definitions(test_mock_udp PRIVATE
    -DSCU_MOCK_UDP_DELAY_MIN_MS=10
    -DSCU_MOCK_UDP_DELAY_MAX_MS=50
    -DSCU_MOCK_UDP_PACKET_LOSS_PERCENTAGE=10
  )

  target_link_libraries(test_mock_udp network_mock)
  scu_sanitize_test(test_mock_udp)

  # Uncomment to enable packet loss and delay simulation in tests
  # However, right now it fails - needs to be fixed (possibly mock udp implementation issue)

  # Test of mock_udp.cpp required for network tests
  # The churn/restart soak tests run tens of seconds of simulated loss on top
  # of the original suite - the 60s ament default is too tight.
  ament_add_gtest(test_scorpio_udp_mock test/network/test_scorpio_udp_mock.cpp TIMEOUT 180)

  target_compile_definitions(test_scorpio_udp_mock PRIVATE
    -DSCU_MOCK_UDP_DELAY_MIN_MS=10
    -DSCU_MOCK_UDP_DELAY_MAX_MS=50
    -DSCU_MOCK_UDP_PACKET_LOSS_PERCENTAGE=10
  )

  target_link_libraries(test_scorpio_udp_mock network_mock ${SANITIZED_SUPPORT_LIBRARIES})
  scu_sanitize_test(test_scorpio_udp_mock)

  # Test of scorpio_udp with gmock for udp.hpp
  ament_add_gtest(test_scorpio_udp_gmock test/network/test_scorpio_udp_gmock.cpp)

  add_library(network_gmock STATIC
    src/network/ip.cpp
    src/network/scorpio_udp.cpp
  )
  target_link_libraries(network_gmock fmt::fmt)
  target_compile_options(network_gmock PRIVATE -std=gnu++17)
  target_compile_definitions(network_gmock PRIVATE
    -DSCORPIO_UTILS_UDP_GMOCK=1
    -DSCU_LOG_LEVEL=5
  )
  target_include_directories(network_gmock PRIVATE SYSTEM ${GMOCK_INCLUDE_DIRS})
  target_link_libraries(network_gmock ${GMOCK_MAIN_LIBRARIES} ${GMOCK_LIBRARIES})
  scu_sanitize(network_gmock)

  target_link_libraries(test_scorpio_udp_gmock network_gmock ${SANITIZED_SUPPORT_LIBRARIES})
  target_include_directories(test_scorpio_udp_gmock PRIVATE SYSTEM ${GMOCK_INCLUDE_DIRS})
  target_link_libraries(test_scorpio_udp_gmock ${GMOCK_MAIN_LIBRARIES} ${GMOCK_LIBRARIES})
  target_compile_options(test_scorpio_udp_gmock PRIVATE -std=gnu++17)
  target_compile_definitions(test_scorpio_udp_gmock PRIVATE
    -DSCORPIO_UTILS_UDP_GMOCK=1
    -DSCU_LOG_LEVEL=5
  )
  scu_sanitize_test(test_scorpio_udp_gmock)

  # Test of scorpio_udp with framework
  ament_add_gtest(test_scorpio_udp_framework test/network/test_scorpio_udp_framework.cpp TIMEOUT 180)

  add_library(network_framework_test STATIC
    src/network/ip.cpp
    src/network/scorpio_udp.cpp
  )
  target_link_libraries(network_framework_test fmt::fmt)
  target_compile_definitions(network_framework_test PRIVATE
    -DSCORPIO_UTILS_FRAMEWORK=1
    -DSCU_LOG_LEVEL=5
    # Shrink the resend-history safety buffer so buffer-boundary behaviour (out-of-history
    # resend requests, QoS-depth overflow) is reachable in a handful of packets under test.
    -DSCU_UDP_QOS_DEPTH_SAFETY_BUFFER=8
  )
  target_include_directories(network_framework_test PRIVATE SYSTEM ${GMOCK_INCLUDE_DIRS})
  target_link_libraries(network_framework_test ${GMOCK_MAIN_LIBRARIES} ${GMOCK_LIBRARIES})
  scu_sanitize(network_framework_test)

  target_link_libraries(test_scorpio_udp_framework network_framework_test ${SANITIZED_SUPPORT_LIBRARIES})
  target_compile_definitions(test_scorpio_udp_framework PRIVATE
    -DSCORPIO_UTILS_FRAMEWORK=1
    -DSCU_LOG_LEVEL=5
    # Same history size as network_framework_test, so the tests can count packets against it
    -DSCU_UDP_QOS_DEPTH_SAFETY_BUFFER=8
  )
  scu_sanitize_test(test_scorpio_udp_framework)
endif()

ament_export_include_directories(include
  # itertools/include
)
ament_package()
