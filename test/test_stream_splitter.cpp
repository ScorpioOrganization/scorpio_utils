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
#include <ios>
#include <iostream>
#include <optional>
#include <sstream>
#include "scorpio_utils/stream_splitter.hpp"

template<bool HasFlush = true, bool HasSeekpPos = true, bool HasSeekpOffset = true>
class MockStream {
  std::stringstream _stream;
  size_t _flush_count;
  std::vector<std::streampos> _seekp_calls;
  std::vector<std::pair<std::streampos, std::ios_base::seekdir>> _seekp_offset_calls;

public:
  inline MockStream& operator<<(const std::string& s) {
    _stream << s;
    return *this;
  }

  template<bool B = HasFlush, typename = std::enable_if_t<B>>
  MockStream& flush() {
    _stream.flush();
    ++_flush_count;
    return *this;
  }

  template<bool B = HasSeekpPos, typename = std::enable_if_t<B>>
  MockStream& seekp(std::streampos pos) {
    _stream.seekp(pos);
    _seekp_calls.push_back(pos);
    return *this;
  }

  template<bool B = HasSeekpOffset, typename = std::enable_if_t<B>>
  MockStream& seekp(std::streamoff offset, std::ios_base::seekdir dir) {
    _stream.seekp(offset, dir);
    _seekp_offset_calls.emplace_back(offset, dir);
    return *this;
  }

  inline std::string str() const {
    return _stream.str();
  }

  inline auto flush_count() const {
    return _flush_count;
  }

  inline const auto& seekp_calls() const {
    return _seekp_calls;
  }

  inline const auto& seekp_offset_calls() const {
    return _seekp_offset_calls;
  }
};

class StreamSplitterSingleStream : public ::testing::Test {
protected:
  MockStream<> _stream1;
  std::optional<scorpio_utils::StreamSplitter<MockStream<true, true, true>>> _splitter;

  void SetUp() override {
    _stream1 = MockStream();
    _splitter.emplace(_stream1);
  }
  void TearDown() override { }
};

TEST_F(StreamSplitterSingleStream, write) {
  *_splitter << "Hello,";
  *_splitter << " World!";
  EXPECT_EQ(_stream1.str(), "Hello, World!") << "Stream should contain the concatenated output";
}

TEST_F(StreamSplitterSingleStream, flush) {
  EXPECT_EQ(_stream1.flush_count(), 0) << "Flush should not be called yet";
  _splitter->flush();
  EXPECT_EQ(_stream1.flush_count(), 1) << "Flush should be called once after explicit flush call";
}

TEST_F(StreamSplitterSingleStream, seekp) {
  EXPECT_TRUE(_stream1.seekp_calls().empty()) << "Seekp should not be called yet";
  _splitter->seekp(5);
  ASSERT_EQ(_stream1.seekp_calls().size(), 1) << "Seekp should be called once after explicit seekp call";
  EXPECT_EQ(_stream1.seekp_calls()[0], 5) << "Seekp should be called with position 5";
}

TEST_F(StreamSplitterSingleStream, seekpOffset) {
  EXPECT_TRUE(_stream1.seekp_offset_calls().empty()) << "Seekp with offset should not be called yet";
  _splitter->seekp(10, std::ios_base::cur);
  ASSERT_EQ(_stream1.seekp_offset_calls().size(), 1) << "Seekp with offset should be called once";
  EXPECT_EQ(_stream1.seekp_offset_calls()[0].first, 10) << "Seekp with offset should be called with offset 10";
  EXPECT_EQ(_stream1.seekp_offset_calls()[0].second,
  std::ios_base::cur) << "Seekp with offset should be called with seekdir cur";
}

class StreamSplitterMultipleStreams : public ::testing::Test {
protected:
  MockStream<false> _stream1;
  MockStream<> _stream2;
  std::optional<scorpio_utils::StreamSplitter<MockStream<false>, MockStream<>>> _splitter;

  void SetUp() override {
    _stream1 = MockStream<false>();
    _stream2 = MockStream<>();
    _splitter.emplace(_stream1, _stream2);
  }

  void TearDown() override { }
};

TEST_F(StreamSplitterMultipleStreams, write) {
  *_splitter << "Hello,";
  *_splitter << " World!";

  EXPECT_EQ(_stream1.str(), "Hello, World!");
  EXPECT_EQ(_stream2.str(), "Hello, World!");
}

TEST_F(StreamSplitterMultipleStreams, flush) {
  EXPECT_EQ(_stream1.flush_count(), 0) << "Flush should not be called yet";
  EXPECT_EQ(_stream2.flush_count(), 0) << "Flush should not be called yet";
  _splitter->flush();
  EXPECT_EQ(_stream1.flush_count(), 0) << "Flush should not be called for _stream1";
  EXPECT_EQ(_stream2.flush_count(), 1) << "Flush should be called once for _stream2";
}

TEST_F(StreamSplitterMultipleStreams, seekp) {
  EXPECT_TRUE(_stream1.seekp_calls().empty()) << "Seekp should not be called yet";
  EXPECT_TRUE(_stream2.seekp_calls().empty()) << "Seekp should not be called yet";
  _splitter->seekp(5);
  ASSERT_EQ(_stream1.seekp_calls().size(), 1) << "Seekp should be called once for _stream1";
  EXPECT_EQ(_stream1.seekp_calls()[0], 5) << "Seekp should be called with position 5 for _stream1";
  ASSERT_EQ(_stream2.seekp_calls().size(), 1) << "Seekp should be called once for _stream2";
  EXPECT_EQ(_stream2.seekp_calls()[0], 5) << "Seekp should be called with position 5 for _stream2";
}

TEST_F(StreamSplitterMultipleStreams, seekpOffset) {
  EXPECT_TRUE(_stream1.seekp_offset_calls().empty());
  EXPECT_TRUE(_stream2.seekp_offset_calls().empty());
  _splitter->seekp(10, std::ios_base::cur);
  ASSERT_EQ(_stream1.seekp_offset_calls().size(), 1) << "Seekp with offset should be called once for _stream1";
  EXPECT_EQ(_stream1.seekp_offset_calls()[0].first,
  10) << "Seekp with offset should be called with offset 10 for _stream1";
  EXPECT_EQ(_stream1.seekp_offset_calls()[0].second, std::ios_base::cur)
    << "Seekp with offset should be called with seekdir cur for _stream1";
  ASSERT_EQ(_stream2.seekp_offset_calls().size(), 1) << "Seekp with offset should be called once for _stream2";
  EXPECT_EQ(_stream2.seekp_offset_calls()[0].first, 10)
    << "Seekp with offset should be called with offset 10 for _stream2";
  EXPECT_EQ(_stream2.seekp_offset_calls()[0].second, std::ios_base::cur)
    << "Seekp with offset should be called with seekdir cur for _stream2";
}
