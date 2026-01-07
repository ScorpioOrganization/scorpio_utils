#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "scorpio_utils/buffered_file_saver.hpp"

#define EXAMPLE_FILENAME "file.txt"

TEST(BufferedFileSaver, BasicUsage) {
  ASSERT_FALSE(std::filesystem::is_regular_file(EXAMPLE_FILENAME)) << "To test BufferedFileSaver file named: " <<
    EXAMPLE_FILENAME << " should not exist";
  {
    scorpio_utils::BufferedFileSaver file(EXAMPLE_FILENAME);
    file << "Help! world!";
    file.seekp(3);
    file << "lo";
    file.seekp(0, std::ios_base::end);
    file << '\n';
    EXPECT_TRUE(file.flush());
  }
  ASSERT_TRUE(std::filesystem::is_regular_file(EXAMPLE_FILENAME));
  std::ifstream file(EXAMPLE_FILENAME);
  std::string file_content;
  EXPECT_TRUE(std::getline(file, file_content));
  EXPECT_EQ(file_content, "Hello world!");
  EXPECT_FALSE(std::getline(file, file_content));
  EXPECT_TRUE(std::filesystem::remove(EXAMPLE_FILENAME));
}
