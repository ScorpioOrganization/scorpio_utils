cmake_minimum_required(VERSION 3.8)
project(scorpio_utils)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)

add_compile_options(-std=c++17 -Wall -Wextra -Wpedantic -pedantic-errors -Wdeprecated
  -Wc++17-compat -Wconversion -Wsign-conversion -Wpessimizing-move -Werror -flto=auto)

add_link_options(-flto=auto)

include_directories(
  include/
)

add_library(threading SHARED
  src/threading/signal.cpp
  src/threading/thread_pool.cpp
  src/threading/thread_streams.cpp
  src/threading/waitable.cpp
)
target_include_directories(threading PRIVATE
  include/scorpio_utils
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

set(LIBRARY_LIST
  buffered_file_saver
  gps
  lifetime_helper
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
  find_package(ament_cmake_gtest REQUIRED)

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
endif()

ament_export_include_directories(include)
ament_package()
