#include <gtest/gtest.h>
#include <atomic>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>
#include "scorpio_utils/testing/lifetime_helper.hpp"
#include "scorpio_utils/threading/channel.hpp"

static_assert(std::is_trivially_destructible_v<scorpio_utils::threading::Channel<int, 10>>);
static_assert(!std::is_trivially_destructible_v<scorpio_utils::threading::Channel<std::string, 10>>);

TEST(Channel, BasicFunctionality) {
  scorpio_utils::threading::Channel<int, 256> channel;

  for (int i = 0; i < 300; ++i) {
    EXPECT_EQ(channel.send(i), i >= 256) << "Send " << i;
  }
  for (int i = 0; i < 256; ++i) {
    auto value = channel.receive();
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(value.value(), i);
  }
  for (int i = 256; i < 300; ++i) {
    EXPECT_FALSE(channel.receive().has_value());
  }
}

TEST(Channel, MutipleProducers) {
  scorpio_utils::threading::Channel<size_t, 1024> channel;

  std::atomic<bool> start(false);
  std::atomic<size_t> sent_count(0);
  std::atomic<size_t> sender_count(0);
  std::vector<std::thread> threads;
  threads.reserve(20);
  for (int i = 0; i < 10; ++i) {
    threads.emplace_back([&channel, &sent_count, &start, &sender_count]() {
        sender_count.fetch_add(1);
        while (!start.load()) {
          std::this_thread::yield();
        }
        for (int j = 0; j < 100; ++j) {
          ASSERT_FALSE(channel.send(sent_count++));
        }
        sender_count.fetch_sub(1);
    });
  }
  std::array<std::atomic<uint16_t>, 1000> received_values;
  for (auto& x : received_values) {
    x = 0;
  }
  while (sender_count.load() != 10) {
    std::this_thread::yield();
  }
  for (int i = 0; i < 10; ++i) {
    threads.emplace_back([&channel, &start, &received_values, &sender_count]() {
        while (!start.load()) {
          std::this_thread::yield();
        }
        while (sender_count.load() > 0 || channel.available() > 0) {
          std::optional<size_t> item = channel.receive();
          if (!item.has_value()) {
            std::this_thread::yield();
            continue;
          }
          ASSERT_LT(item.value(), 1000);
          ++received_values[*item];
        }
    });
  }
  start.store(true);
  for (auto& producer : threads) {
    producer.join();
  }
  for (size_t i = 0; i < received_values.size(); ++i) {
    ASSERT_EQ(received_values[i].load(), 1) << "Value " << i << " shall be set to 1";
  }
}

TEST(Channel, HighLoad) {
  scorpio_utils::threading::Channel<size_t, 1024> channel;
  EXPECT_EQ(channel.size(), 1024) << "Channel size should be 1024";
  EXPECT_TRUE(channel.is_empty()) << "Channel should be empty at start";
  EXPECT_TRUE(channel.is_write_ready()) << "Channel should be writable at start";
  EXPECT_FALSE(channel.is_full()) << "Channel should not be full at start";
  EXPECT_EQ(channel.available(), 0) << "Available items should be 0 at start";

  std::atomic<bool> start(false);
  std::vector<std::thread> threads;
  std::atomic<int> sent_count(0);
  std::atomic<int> received_count(0);
  threads.reserve(20);
  std::atomic<size_t> sender_count(0);
  std::atomic<size_t> send_value(0);

  std::array<std::atomic<int32_t>, 100000> received_values;
  for (auto& x : received_values) {
    x.store(0, std::memory_order_relaxed);
  }

  for (int i = 0; i < 10; ++i) {
    threads.emplace_back([&channel, &sent_count, &start, &sender_count, &send_value, &received_values]() {
        sender_count.fetch_add(1);
        while (!start.load()) {
          std::this_thread::yield();
        }
        for (int j = 0; j < 10000; ++j) {
          auto v = send_value.fetch_add(1, std::memory_order_relaxed);
          if (!channel.send(v)) {
            sent_count.fetch_add(1, std::memory_order_relaxed);
            received_values[v].fetch_add(1, std::memory_order_relaxed);
          }
        }
        sender_count.fetch_sub(1);
    });
  }

  while (sender_count.load() == 0) {
    std::this_thread::yield();
  }

  for (int i = 0; i < 10; ++i) {
    threads.emplace_back([&channel, &received_count, &start, &sender_count, &received_values]() {
        while (!start.load()) {
          std::this_thread::yield();
        }
        while (sender_count.load() > 0 || channel.available() > 0) {
          auto item = channel.receive();
          if (item.has_value()) {
            received_count.fetch_add(1, std::memory_order_relaxed);
            received_values[item.value()].fetch_sub(1, std::memory_order_relaxed);
          } else {
            std::this_thread::yield();  // Interestingly without this yield the test can hang
          }
        }
    });
  }

  start.store(true);
  for (auto& thread : threads) {
    thread.join();
  }
  EXPECT_EQ(sent_count.load(std::memory_order_relaxed),
    received_count.load(std::memory_order_relaxed)) << "Sent and received counts should match";

  for (size_t i = 0; i < received_values.size(); ++i) {
    EXPECT_EQ(received_values[i].load(std::memory_order_relaxed), 0);
  }
}

TEST(Channel, CopyOnlyType) {
  class CopyOnly {
    int value;

public:
    explicit CopyOnly(int v)
    : value(v) { }
    CopyOnly(const CopyOnly&) = default;
    CopyOnly& operator=(const CopyOnly&) = default;
    CopyOnly(CopyOnly&&) = delete;
    CopyOnly& operator=(CopyOnly&&) = delete;
    auto get_value() const {
      return value;
    }
  };

  scorpio_utils::threading::Channel<CopyOnly, 1> channel;

  CopyOnly item(3);
  EXPECT_FALSE(channel.send(item)) << "Should be able to send copyable type";
  auto received_item = channel.receive();
  ASSERT_TRUE(received_item.has_value()) << "Should receive an item";
  EXPECT_EQ(received_item->get_value(), 3) << "Item should be copied once";
}

TEST(Channel, CopyOnlyWithDefaultType) {
  class CopyOnly {
public:
    CopyOnly() = default;
    CopyOnly(const CopyOnly&) = default;
    CopyOnly& operator=(const CopyOnly&) = default;
    CopyOnly(CopyOnly&&) = delete;
    CopyOnly& operator=(CopyOnly&&) = delete;
  };

  scorpio_utils::threading::Channel<CopyOnly, 1> channel;

  CopyOnly item;
  EXPECT_FALSE(channel.send(item)) << "Should be able to send copyable type";
  auto received_item = channel.receive();
  ASSERT_TRUE(received_item.has_value()) << "Should receive an item";
}

TEST(Channel, MoveOnlyType) {
  scorpio_utils::threading::Channel<std::unique_ptr<int>, 1> channel;
  std::unique_ptr<int> item = std::make_unique<int>(42);
  EXPECT_EQ(channel.size(), 1) << "Channel size should be 1";
  EXPECT_TRUE(channel.is_empty()) << "Channel should be empty at start";
  EXPECT_TRUE(channel.is_write_ready()) << "Channel should be writable at start";
  EXPECT_FALSE(channel.is_full()) << "Channel should not be full at start";
  EXPECT_EQ(channel.available(), 0) << "Available items should be 0 at start";

  EXPECT_FALSE(channel.send(std::move(item))) << "Should be able to send move-only type";

  EXPECT_FALSE(item) << "Item should be moved and no longer valid";
  EXPECT_EQ(channel.size(), 1) << "Channel size should be 1";
  EXPECT_FALSE(channel.is_empty()) << "Channel should not be empty after sending an item";
  EXPECT_FALSE(channel.is_write_ready()) << "Channel should still be writable after sending an item";
  EXPECT_TRUE(channel.is_full()) << "Channel should not be full after sending an item";
  EXPECT_EQ(channel.available(), 1) << "Available items should be 1 after sending an item";

  auto received_item = channel.receive();
  ASSERT_TRUE(received_item.has_value()) << "Should receive an item";
  EXPECT_EQ(*received_item->get(), 42) << "Item should be moved once";
}

TEST(Channel, LifetimeCheck) {
  scorpio_utils::threading::Channel<scorpio_utils::testing::LifetimeHelper, 1> channel;

  scorpio_utils::testing::LifetimeHelper item;
  EXPECT_FALSE(channel.send(std::move(item))) << "Should be able to send item";
  auto received_item = channel.receive();
  EXPECT_EQ(item.get_copy_count(), 0) << "Item should not be copied";
  EXPECT_EQ(item.get_move_count(), 1) << "Item should be moved once";
  ASSERT_EQ(item.get_event_log().size(), 2) << "Item should have two events in the log";
  EXPECT_EQ(item.get_event_log()[0],
    scorpio_utils::testing::LifetimeHelper::EventType::CREATED) << "First event should be CREATED";
  EXPECT_EQ(item.get_event_log()[1],
    scorpio_utils::testing::LifetimeHelper::EventType::HAS_BEEN_MOVED) << "Second event should be HAS_BEEN_MOVED";
  ASSERT_TRUE(received_item.has_value()) << "Should receive an item";
  EXPECT_EQ(received_item->get_event_log().size(), 1) << "Received item should have two events in the log";
  EXPECT_EQ(received_item->get_event_log()[0],
    scorpio_utils::testing::LifetimeHelper::EventType::MOVE) << "First event should be CREATED";
  ASSERT_EQ(received_item->get_value_event_log().size(),
    3) << "Received item should have three events in the value log";
  EXPECT_EQ(received_item->get_value_event_log()[0],
    scorpio_utils::testing::LifetimeHelper::EventType::CREATED) << "First event in value log should be CREATED";
  EXPECT_EQ(received_item->get_value_event_log()[1],
    scorpio_utils::testing::LifetimeHelper::EventType::MOVE) << "Second event in value log should be MOVE";
}

TEST(Channel, ForceSendReceive) {
  scorpio_utils::threading::Channel<int, 1> channel;

  EXPECT_FALSE(channel.send(42)) << "Should be able to send item";
  EXPECT_TRUE(channel.send(43)) << "Should not be able to send item when channel is full";

  auto received_item = channel.receive<true>();
  EXPECT_EQ(received_item, 42) << "Received item should be 42";

  channel.send<true>(44);
  received_item = channel.receive<true>();
  EXPECT_EQ(received_item, 44) << "Received item should be 44";
}

TEST(Channel, CloseChannel) {
  scorpio_utils::threading::Channel<int, 10> channel;

  std::thread receiver([&channel]() {
      EXPECT_THROW(channel.receive<true>(), scorpio_utils::threading::ClosedChannelException);
    });
  channel.close();
  EXPECT_THROW(channel.send<false>(1), scorpio_utils::threading::ClosedChannelException);
  EXPECT_THROW(channel.send<true>(1), scorpio_utils::threading::ClosedChannelException);
  EXPECT_THROW(channel.receive<false>(), scorpio_utils::threading::ClosedChannelException);
  EXPECT_THROW(channel.receive<true>(), scorpio_utils::threading::ClosedChannelException);
  if (receiver.joinable()) {
    receiver.join();
  }
}

TEST(Channel, SplitChannel) {
  auto channel = std::make_shared<scorpio_utils::threading::Channel<int, 10>>();
  auto [writer, reader] = scorpio_utils::threading::split_channel(channel);

  std::thread receiver([&reader]() {
      auto item = reader.receive<true>();
      EXPECT_EQ(item, 42) << "Received item should be 42";
    });
  writer.send<true>(42);
  if (receiver.joinable()) {
    receiver.join();
  }
}
