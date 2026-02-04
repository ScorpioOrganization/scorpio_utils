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
