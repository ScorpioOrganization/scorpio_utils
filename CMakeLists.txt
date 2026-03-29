cmake_minimum_required(VERSION 3.8)
project(scorpio_utils)

find_package(ament_cmake REQUIRED)
find_package(backward_ros REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)
find_package(fmt REQUIRED)
find_package(magic_enum CONFIG REQUIRED)

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

add_compile_options(-std=c++17 -Wall -Wextra -Wpedantic -pedantic-errors -Wdeprecated
  -Wc++17-compat -Wconversion -Wsign-conversion -Wpessimizing-move -Werror
  # -flto=auto
)

# add_link_options(-flto=auto)

include_directories(
  include/
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
target_link_libraries(network magic_enum::magic_enum fmt::fmt)

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
    time_provider/test_system_time_provider.cpp
    time_utils/test_time_string.cpp
    test_buffered_file_saver.cpp
    test_compare.cpp
    test_defer.cpp
    test_endianness.cpp
    test_expected.cpp
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
    target_link_libraries(${TEST_NAME} ${LIBRARY_LIST})
    ament_target_dependencies(${TEST_NAME} rclcpp std_msgs)
  endforeach()

  add_library(network_mock STATIC
    test/network/mock_udp.cpp
    src/network/ip.cpp
    src/network/scorpio_udp.cpp
  )
  target_link_libraries(network_mock fmt::fmt magic_enum::magic_enum)

  # Test of mock_udp.cpp required for network tests
  ament_add_gtest(test_mock_udp test/network/test_udp.cpp)

  target_compile_definitions(test_mock_udp PRIVATE
    -DSCU_MOCK_UDP_DELAY_MIN_MS=10
    -DSCU_MOCK_UDP_DELAY_MAX_MS=50
    -DSCU_MOCK_UDP_PACKET_LOSS_PERCENTAGE=10
  )

  target_link_libraries(test_mock_udp network_mock)

  # Uncomment to enable packet loss and delay simulation in tests
  # However, right now it fails - needs to be fixed (possibly mock udp implementation issue)

  # Test of mock_udp.cpp required for network tests
  ament_add_gtest(test_scorpio_udp_mock test/network/test_scorpio_udp_mock.cpp)

  target_compile_definitions(test_scorpio_udp_mock PRIVATE
    -DSCU_MOCK_UDP_DELAY_MIN_MS=10
    -DSCU_MOCK_UDP_DELAY_MAX_MS=50
    -DSCU_MOCK_UDP_PACKET_LOSS_PERCENTAGE=10
  )

  target_link_libraries(test_scorpio_udp_mock
    buffered_file_saver
    gps
    lifetime_helper
    network_mock
    string_utils
    time_provider
    time_utils
    threading
  )

  # Test of scorpio_udp with gmock for udp.hpp
  ament_add_gtest(test_scorpio_udp_gmock test/network/test_scorpio_udp_gmock.cpp)

  add_library(network_gmock STATIC
    src/network/ip.cpp
    src/network/scorpio_udp.cpp
  )
  target_link_libraries(network_gmock fmt::fmt magic_enum::magic_enum)
  target_compile_options(network_gmock PRIVATE -std=gnu++17)
  target_compile_definitions(network_gmock PRIVATE
    -DSCORPIO_UTILS_UDP_GMOCK=1
    -DSCU_LOG_LEVEL=5
  )
  target_include_directories(network_gmock PRIVATE SYSTEM ${GMOCK_INCLUDE_DIRS})
  target_link_libraries(network_gmock ${GMOCK_MAIN_LIBRARIES} ${GMOCK_LIBRARIES})

  target_link_libraries(test_scorpio_udp_gmock
    buffered_file_saver
    gps
    lifetime_helper
    network_gmock
    string_utils
    time_provider
    time_utils
    threading
  )
  target_include_directories(test_scorpio_udp_gmock PRIVATE SYSTEM ${GMOCK_INCLUDE_DIRS})
  target_link_libraries(test_scorpio_udp_gmock ${GMOCK_MAIN_LIBRARIES} ${GMOCK_LIBRARIES})
  target_compile_options(test_scorpio_udp_gmock PRIVATE -std=gnu++17)
  target_compile_definitions(test_scorpio_udp_gmock PRIVATE
    -DSCORPIO_UTILS_UDP_GMOCK=1
    -DSCU_LOG_LEVEL=5
  )

  # Test of scorpio_udp with framework
  ament_add_gtest(test_scorpio_udp_framework test/network/test_scorpio_udp_framework.cpp)

  add_library(network_framework_test STATIC
    src/network/ip.cpp
    src/network/scorpio_udp.cpp
  )
  target_link_libraries(network_framework_test fmt::fmt magic_enum::magic_enum)
  target_compile_definitions(network_framework_test PRIVATE
    -DSCORPIO_UTILS_FRAMEWORK=1
    -DSCU_LOG_LEVEL=5
  )
  target_include_directories(network_framework_test PRIVATE SYSTEM ${GMOCK_INCLUDE_DIRS})
  target_link_libraries(network_framework_test ${GMOCK_MAIN_LIBRARIES} ${GMOCK_LIBRARIES})

  target_link_libraries(test_scorpio_udp_framework
    buffered_file_saver
    gps
    lifetime_helper
    network_framework_test
    string_utils
    time_provider
    time_utils
    threading
  )
  target_compile_definitions(test_scorpio_udp_framework PRIVATE
    -DSCORPIO_UTILS_FRAMEWORK=1
    -DSCU_LOG_LEVEL=5
  )
endif()

ament_export_include_directories(include)
ament_package()
