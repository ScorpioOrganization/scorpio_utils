#include <gtest/gtest.h>

#include <thread>
#include <atomic>

#include "scorpio_utils/threading/atomic_ring_buffer.hpp"

TEST(AtomicRingBuffer, BasicFunctionality) {
  scorpio_utils::threading::AtomicRingBuffer<int> ring_buffer(4);
  for (int i = 0; i < 6; ++i) {
    EXPECT_EQ(ring_buffer.push(i), i < 4) << "Push " << i;
  }
  for (int i = 0; i < 4; ++i) {
    auto value = ring_buffer.pop();
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(value.value(), i + 2);
  }
  for (int i = 4; i < 6; ++i) {
    EXPECT_FALSE(ring_buffer.pop().has_value());
  }
}

TEST(AtomicRingBuffer, EmptyFullChecks) {
  scorpio_utils::threading::AtomicRingBuffer<int> ring_buffer(2);
  EXPECT_TRUE(ring_buffer.empty()) << "Ring buffer should be empty initially";
  EXPECT_FALSE(ring_buffer.full()) << "Ring buffer should not be full initially";

  ring_buffer.push(1);
  EXPECT_FALSE(ring_buffer.empty()) << "Ring buffer should not be empty after one push";
  EXPECT_FALSE(ring_buffer.full()) << "Ring buffer should not be full after one push";

  ring_buffer.push(2);
  EXPECT_FALSE(ring_buffer.empty()) << "Ring buffer should not be empty after two pushes";
  EXPECT_TRUE(ring_buffer.full()) << "Ring buffer should be full after two pushes";

  ring_buffer.pop();
  EXPECT_FALSE(ring_buffer.empty()) << "Ring buffer should not be empty after one pop";
  EXPECT_FALSE(ring_buffer.full()) << "Ring buffer should not be full after one pop";

  ring_buffer.pop();
  EXPECT_TRUE(ring_buffer.empty()) << "Ring buffer should be empty after popping all elements";
  EXPECT_FALSE(ring_buffer.full()) << "Ring buffer should not be full when empty";
}

TEST(AtomicRingBuffer, OverwriteBehavior) {
  scorpio_utils::threading::AtomicRingBuffer<int> ring_buffer(3);
  ring_buffer.push(1);
  ring_buffer.push(2);
  ring_buffer.push(3);
  EXPECT_TRUE(ring_buffer.full()) << "Ring buffer should be full after three pushes";

  ring_buffer.push(4);

  auto value = ring_buffer.pop();
  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(value.value(), 2) << "Oldest element should be overwritten, expected 2";

  value = ring_buffer.pop();
  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(value.value(), 3) << "Next element should be 3";

  value = ring_buffer.pop();
  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(value.value(), 4) << "Next element should be 4";

  EXPECT_FALSE(ring_buffer.pop().has_value()) << "Ring buffer should be empty now";
}

TEST(AtomicRingBuffer, NonTrivialType) {
  struct NonTrivial {
    NonTrivial(int v)  // NOLINT
    : value(v) { }
    NonTrivial(const NonTrivial& other)
    : value(other.value) { }
    NonTrivial& operator=(const NonTrivial& other) {
      value = other.value;
      return *this;
    }
    int value;
  };

  scorpio_utils::threading::AtomicRingBuffer<NonTrivial> ring_buffer(2);
  ring_buffer.push(NonTrivial(10));
  ring_buffer.push(NonTrivial(20));

  auto item = ring_buffer.pop();
  ASSERT_TRUE(item.has_value());
  EXPECT_EQ(item->value, 10);

  item = ring_buffer.pop();
  ASSERT_TRUE(item.has_value());
  EXPECT_EQ(item->value, 20);
}

TEST(AtomicRingBuffer, PowerOfTwoSize) {
  scorpio_utils::threading::AtomicRingBuffer<int> ring_buffer(8);
  for (int i = 0; i < 10; ++i) {
    ring_buffer.push(i);
  }
  for (int i = 2; i < 10; ++i) {
    auto value = ring_buffer.pop();
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(value.value(), i);
  }
  EXPECT_FALSE(ring_buffer.pop().has_value());
}

TEST(AtomicRingBuffer, SizeOneEdgeCase) {
  scorpio_utils::threading::AtomicRingBuffer<int> ring_buffer(1);

  EXPECT_TRUE(ring_buffer.empty());
  EXPECT_FALSE(ring_buffer.full());
  EXPECT_FALSE(ring_buffer.pop().has_value());

  EXPECT_TRUE(ring_buffer.push(1)) << "Push 1 should not overwrite";
  EXPECT_FALSE(ring_buffer.empty());
  EXPECT_TRUE(ring_buffer.full());

  EXPECT_FALSE(ring_buffer.push(2)) << "Push 2 should overwrite";
  EXPECT_FALSE(ring_buffer.empty());
  EXPECT_TRUE(ring_buffer.full());

  auto value = ring_buffer.pop();
  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(value.value(), 2);
  EXPECT_TRUE(ring_buffer.empty());
  EXPECT_FALSE(ring_buffer.full());

  EXPECT_FALSE(ring_buffer.pop().has_value());
}

TEST(AtomicRingBuffer, MoveSemantics) {
  struct MovableType {
    std::string data;
    bool moved = false;
    MovableType(std::string d)  // NOLINT
    : data(std::move(d)) { }
    MovableType(MovableType&& other) noexcept
    : data(std::move(other.data)), moved(false) {
      other.moved = true;
    }
    MovableType& operator=(MovableType&& other) noexcept {
      data = std::move(other.data);
      other.moved = true;
      return *this;
    }
  };

  scorpio_utils::threading::AtomicRingBuffer<MovableType> ring_buffer(2);

  MovableType item1("A");
  ring_buffer.push(std::move(item1));
  EXPECT_TRUE(item1.moved) << "Source object should be marked as moved-from after push(T&&)";

  auto opt_item = ring_buffer.pop();
  ASSERT_TRUE(opt_item.has_value());
  EXPECT_EQ(opt_item->data, "A");

  MovableType item2("B");
  ring_buffer.push(std::move(item2));
  MovableType item3("C");
  ring_buffer.push(std::move(item3));
  EXPECT_TRUE(ring_buffer.full());

  MovableType item4("D");
  ring_buffer.push(std::move(item4));
  EXPECT_TRUE(item4.moved);

  auto item_c = ring_buffer.pop();
  auto item_d = ring_buffer.pop();
  EXPECT_EQ(item_c->data, "C");
  EXPECT_EQ(item_d->data, "D");
}

TEST(AtomicRingBuffer, OverwriteReturnLogic) {
  scorpio_utils::threading::AtomicRingBuffer<int> ring_buffer(3);

  EXPECT_TRUE(ring_buffer.push(1)) << "Push 1: Should not overwrite (ret=true)";
  EXPECT_FALSE(ring_buffer.full());

  EXPECT_TRUE(ring_buffer.push(2)) << "Push 2: Should not overwrite (ret=true)";
  EXPECT_FALSE(ring_buffer.full());

  EXPECT_TRUE(ring_buffer.push(3)) << "Push 3: Fills, but should not overwrite (ret=true)";
  EXPECT_TRUE(ring_buffer.full());

  EXPECT_FALSE(ring_buffer.push(4)) << "Push 4: Should overwrite '1' (ret=false)";
  EXPECT_TRUE(ring_buffer.full());

  ring_buffer.pop();
  EXPECT_FALSE(ring_buffer.full());

  EXPECT_TRUE(ring_buffer.push(5)) << "Push 5: Should not overwrite '3' (ret=true)";
  EXPECT_TRUE(ring_buffer.full());

  EXPECT_EQ(ring_buffer.pop().value(), 3);
  EXPECT_EQ(ring_buffer.pop().value(), 4);
  EXPECT_EQ(ring_buffer.pop().value(), 5);
}

TEST(AtomicRingBuffer, InterleavedPushPop) {
  scorpio_utils::threading::AtomicRingBuffer<int> ring_buffer(5);
  int pushed_count = 0;
  int popped_count = 0;

  for (int i = 0; i < 3; ++i) {
    ring_buffer.push(i);
    pushed_count++;
  }
  EXPECT_EQ(pushed_count, 3);
  EXPECT_FALSE(ring_buffer.empty());
  EXPECT_FALSE(ring_buffer.full());

  for (int i = 0; i < 2; ++i) {
    auto value = ring_buffer.pop();
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(value.value(), i);
    popped_count++;
  }
  EXPECT_EQ(ring_buffer.pop().value(), 2) << "Pop the last element before phase 3";
  popped_count++;
  EXPECT_TRUE(ring_buffer.empty());

  for (int i = 3; i < 8; ++i) {
    ring_buffer.push(i);
    pushed_count++;
  }
  EXPECT_EQ(pushed_count, 8);
  EXPECT_TRUE(ring_buffer.full());

  ring_buffer.push(8);
  pushed_count++;
  EXPECT_EQ(pushed_count, 9);

  for (int expected = 4; expected <= 8; ++expected) {
    auto value = ring_buffer.pop();
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(value.value(), expected);
    popped_count++;
  }

  EXPECT_EQ(popped_count, 8);

  EXPECT_TRUE(ring_buffer.empty());
  EXPECT_FALSE(ring_buffer.pop().has_value());
}

TEST(AtomicRingBuffer, MultiThreadedSPSC) {
  scorpio_utils::threading::AtomicRingBuffer<size_t> ring_buffer(1024 * 1024);
  const size_t num_elements = 100000;
  std::vector<size_t> received_data;
  received_data.reserve(num_elements);

  std::atomic<bool> producer_done{ false };

  auto producer = std::thread([&ring_buffer, &producer_done]() {
        for (size_t i = 0; i < num_elements; ++i) {
          ring_buffer.push(i);
        }
        producer_done.store(true, std::memory_order_release);
  });

  auto consumer = std::thread([&ring_buffer, &received_data, &producer_done]() {
        while (!producer_done.load(std::memory_order_acquire) || !ring_buffer.empty()) {
          auto value = ring_buffer.pop();
          if (value.has_value()) {
            received_data.push_back(value.value());
          } else if (!producer_done.load(std::memory_order_relaxed)) {
            std::this_thread::yield();
          }
        }
  });

  producer.join();
  consumer.join();

  if (received_data.empty()) {
    FAIL() << "No data was received from the buffer.";
  }

  for (size_t i = 0; i < received_data.size() - 1; ++i) {
    EXPECT_EQ(received_data[i] + 1, received_data[i + 1])
      << "Received data is not sequential at index " << i;
  }
}
