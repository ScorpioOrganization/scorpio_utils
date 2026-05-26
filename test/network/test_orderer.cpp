#include <gtest/gtest.h>
#include "scorpio_utils/network/orderer.hpp"

using scorpio_utils::network::Orderer;
using scorpio_utils::network::OrdererAddResult;

TEST(OrdererTest, InitialState) {
  Orderer<int> orderer(3);

  EXPECT_EQ(orderer.get_size(), 3);
  EXPECT_EQ(orderer.get_current_count(), 0);
  EXPECT_EQ(orderer.get_current_index(), 0);
  EXPECT_FALSE(orderer.next().has_value());
}

TEST(OrdererTest, AddElements) {
  Orderer<int> orderer(3);

  EXPECT_EQ(orderer.add(0, 10), OrdererAddResult::SUCCESS);
  EXPECT_EQ(orderer.get_current_count(), 1);
  EXPECT_EQ(orderer.get_current_index(), 0);

  EXPECT_EQ(orderer.add(1, 20), OrdererAddResult::SUCCESS);
  EXPECT_EQ(orderer.get_current_count(), 2);
  EXPECT_EQ(orderer.get_current_index(), 0);

  int value = 30;
  EXPECT_EQ(orderer.add(2, value), OrdererAddResult::SUCCESS);
  EXPECT_EQ(orderer.get_current_count(), 3);
  EXPECT_EQ(orderer.get_current_index(), 0);
}

TEST(OrdererTest, AlreadyPresentElement) {
  Orderer<int> orderer(3);

  EXPECT_EQ(orderer.add(0, 10), OrdererAddResult::SUCCESS);
  EXPECT_EQ(orderer.add(1, 20), OrdererAddResult::SUCCESS);

  // Try to add element at index 1 again
  int x = 25;
  EXPECT_EQ(orderer.add(1, x), OrdererAddResult::ALREADY_PRESENT);
  EXPECT_EQ(orderer.get_current_count(), 2);
  EXPECT_EQ(orderer.get_current_index(), 0);

  EXPECT_EQ(orderer.add(1, 26), OrdererAddResult::ALREADY_PRESENT);
  EXPECT_EQ(orderer.get_current_count(), 2);
  EXPECT_EQ(orderer.get_current_index(), 0);
}

TEST(OrdererTest, NextFunctionality) {
  Orderer<int> orderer(3);

  orderer.add(0, 10);
  orderer.add(1, 20);
  orderer.add(2, 30);

  auto next = orderer.next();
  ASSERT_TRUE(next.has_value());
  EXPECT_EQ(next.value(), 10);
  EXPECT_EQ(orderer.get_current_count(), 2);
  EXPECT_EQ(orderer.get_current_index(), 1);

  next = orderer.next();
  ASSERT_TRUE(next.has_value());
  EXPECT_EQ(next.value(), 20);
  EXPECT_EQ(orderer.get_current_count(), 1);
  EXPECT_EQ(orderer.get_current_index(), 2);

  next = orderer.next();
  ASSERT_TRUE(next.has_value());
  EXPECT_EQ(next.value(), 30);
  EXPECT_EQ(orderer.get_current_count(), 0);
  EXPECT_EQ(orderer.get_current_index(), 3);
}

TEST(OrdererTest, TooNewAndTooOldElements) {
  Orderer<int> orderer(3);

  orderer.add(0, 10);
  orderer.add(1, 20);
  orderer.add(2, 30);
  orderer.next();

  // Add element within acceptable range
  EXPECT_EQ(orderer.add(3, 40), OrdererAddResult::SUCCESS);
  EXPECT_EQ(orderer.get_current_count(), 3);
  EXPECT_EQ(orderer.get_current_index(), 1);

  // Try to add element that's too new
  EXPECT_EQ(orderer.add(4, 50), OrdererAddResult::TOO_NEW);
  EXPECT_EQ(orderer.get_current_count(), 3);
  EXPECT_EQ(orderer.get_current_index(), 1);

  int x = 50;
  EXPECT_EQ(orderer.add(4, x), OrdererAddResult::TOO_NEW);
  EXPECT_EQ(orderer.get_current_count(), 3);
  EXPECT_EQ(orderer.get_current_index(), 1);

  // Try to add element that's too old
  EXPECT_EQ(orderer.add(0, 60), OrdererAddResult::TOO_OLD);
  EXPECT_EQ(orderer.get_current_count(), 3);
  EXPECT_EQ(orderer.get_current_index(), 1);

  x = 60;
  EXPECT_EQ(orderer.add(0, x), OrdererAddResult::TOO_OLD);
  EXPECT_EQ(orderer.get_current_count(), 3);
  EXPECT_EQ(orderer.get_current_index(), 1);
}

TEST(OrdererTest, SetSizeWithElements) {
  Orderer<int> orderer(3);

  orderer.add(0, 10);
  orderer.add(1, 20);
  orderer.add(2, 30);
  orderer.next();
  orderer.next();
  orderer.next();

  // set_size clears the buffer and resets state
  EXPECT_TRUE(orderer.set_size(7));
  EXPECT_EQ(orderer.get_size(), 7);
  EXPECT_EQ(orderer.get_current_count(), 0);
  EXPECT_EQ(orderer.get_current_index(), 3);

  // Buffer is now empty, so next() should return nothing
  auto next = orderer.next();
  EXPECT_FALSE(next.has_value());

  // Should succeed to change size when buffer is empty
  EXPECT_TRUE(orderer.set_size(1));
  EXPECT_EQ(orderer.get_size(), 1);
  EXPECT_EQ(orderer.get_current_count(), 0);
  EXPECT_EQ(orderer.get_current_index(), 3);
}

TEST(OrdererTest, SetSizeFailsWithElements) {
  Orderer<int> orderer(3);

  // Add some elements but don't consume them all
  orderer.add(0, 10);
  orderer.add(1, 20);
  orderer.add(2, 30);

  // set_size should fail when there are still elements in the buffer
  EXPECT_FALSE(orderer.set_size(5));
  EXPECT_EQ(orderer.get_size(), 3);  // Size should remain unchanged
  EXPECT_EQ(orderer.get_current_count(), 3);  // Count should remain unchanged
  EXPECT_EQ(orderer.get_current_index(), 0);  // Index should remain unchanged

  // Consume one element
  auto next = orderer.next();
  ASSERT_TRUE(next.has_value());
  EXPECT_EQ(next.value(), 10);

  // set_size should still fail because there are still 2 elements
  EXPECT_FALSE(orderer.set_size(2));
  EXPECT_EQ(orderer.get_size(), 3);
  EXPECT_EQ(orderer.get_current_count(), 2);
  EXPECT_EQ(orderer.get_current_index(), 1);

  // Consume remaining elements
  next = orderer.next();
  EXPECT_EQ(next.value(), 20);
  next = orderer.next();
  EXPECT_EQ(next.value(), 30);

  // Now set_size should succeed because buffer is empty
  EXPECT_TRUE(orderer.set_size(1));
  EXPECT_EQ(orderer.get_size(), 1);
  EXPECT_EQ(orderer.get_current_count(), 0);
}

TEST(OrdererTest, SetSizeAndAddNewElement) {
  Orderer<int> orderer(3);

  // Fill and empty the orderer
  orderer.add(0, 10);
  orderer.add(1, 20);
  orderer.add(2, 30);
  orderer.next();
  orderer.next();
  orderer.next();
  orderer.next();

  // Change size to 1
  orderer.set_size(1);

  // After set_size, the current_index is preserved but buffer size is now 1
  // So add(4, 60) would be TOO_NEW because current_index is 3 and buffer size is 1
  EXPECT_EQ(orderer.add(4, 60), OrdererAddResult::TOO_NEW);
  EXPECT_EQ(orderer.get_current_count(), 0);
  EXPECT_EQ(orderer.get_current_index(), 3);

  // Add element within acceptable range (current_index + buffer_size = 3 + 1 = 4)
  EXPECT_EQ(orderer.add(3, 60), OrdererAddResult::SUCCESS);
  EXPECT_EQ(orderer.get_current_count(), 1);
  EXPECT_EQ(orderer.get_current_index(), 3);

  auto next = orderer.next();
  ASSERT_TRUE(next.has_value());
  EXPECT_EQ(next.value(), 60);
  EXPECT_EQ(orderer.get_current_count(), 0);
  EXPECT_EQ(orderer.get_current_index(), 4);
}

TEST(OrdererTest, PeekFunctionality) {
  Orderer<int> orderer(3);

  // Initially, peek should return empty optional
  EXPECT_FALSE(orderer.peek().has_value());

  // Add elements
  orderer.add(0, 10);
  orderer.add(1, 20);
  orderer.add(2, 30);

  // Peek should return the element at current index without removing it
  auto peeked = orderer.peek();
  ASSERT_TRUE(peeked.has_value());
  EXPECT_EQ(peeked.value(), 10);
  EXPECT_EQ(orderer.get_current_count(), 3);  // Count should remain unchanged
  EXPECT_EQ(orderer.get_current_index(), 0);  // Index should remain unchanged

  // Peek again should return the same element
  peeked = orderer.peek();
  ASSERT_TRUE(peeked.has_value());
  EXPECT_EQ(peeked.value(), 10);

  // After next(), peek should return the next element
  auto next = orderer.next();
  EXPECT_EQ(next.value(), 10);
  EXPECT_EQ(orderer.get_current_index(), 1);

  peeked = orderer.peek();
  ASSERT_TRUE(peeked.has_value());
  EXPECT_EQ(peeked.value(), 20);
  EXPECT_EQ(orderer.get_current_count(), 2);  // Count should be decremented by next()
  EXPECT_EQ(orderer.get_current_index(), 1);  // Index should be advanced by next()

  // Move to next element
  next = orderer.next();
  EXPECT_EQ(next.value(), 20);
  peeked = orderer.peek();
  ASSERT_TRUE(peeked.has_value());
  EXPECT_EQ(peeked.value(), 30);

  // Get last element
  next = orderer.next();
  EXPECT_EQ(next.value(), 30);

  // After all elements are consumed, peek should return empty
  peeked = orderer.peek();
  EXPECT_FALSE(peeked.has_value());
}

TEST(OrdererTest, GetContainedFunctionality) {
  Orderer<int> orderer(5);

  // Initially, get_contained should return a single range from 0 to current_index (0)
  auto contained = orderer.get_contained();
  ASSERT_EQ(contained.size(), 1);
  EXPECT_EQ(contained[0], std::make_pair(0ul, 0ul));

  // Add some elements
  orderer.add(0, 10);
  orderer.add(2, 30);
  orderer.add(4, 50);

  // Now get_contained should reflect the added elements
  contained = orderer.get_contained();
  ASSERT_EQ(contained.size(), 3);
  EXPECT_EQ(contained[0], std::make_pair(0ul, 1ul));  // Initial range
  EXPECT_EQ(contained[1], std::make_pair(2ul, 3ul));  // Element at index 0
  EXPECT_EQ(contained[2], std::make_pair(4ul, 5ul));  // Element at index 2

  // Consume one element
  auto next = orderer.next();
  EXPECT_EQ(next.value(), 10);

  // After consuming, get_contained should update accordingly
  contained = orderer.get_contained();
  ASSERT_EQ(contained.size(), 3);
  EXPECT_EQ(contained[0], std::make_pair(0ul, 1ul));  // Updated initial range
  EXPECT_EQ(contained[1], std::make_pair(2ul, 3ul));  // Element at index 2
  EXPECT_EQ(contained[2], std::make_pair(4ul, 5ul));  // Element at index 4

  // Add more elements to create contiguous ranges
  orderer.add(1, 20);
  orderer.add(3, 40);

  contained = orderer.get_contained();
  ASSERT_EQ(contained.size(), 1);
  EXPECT_EQ(contained[0], std::make_pair(0ul, 5ul));  // Contiguous range from index 0 to 3

  // Consume all elements
  next = orderer.next();
  EXPECT_EQ(next.value(), 20);
  next = orderer.next();
  EXPECT_EQ(next.value(), 30);
  next = orderer.next();
  EXPECT_EQ(next.value(), 40);
  next = orderer.next();
  EXPECT_EQ(next.value(), 50);
  EXPECT_FALSE(orderer.next().has_value());
  EXPECT_EQ(orderer.get_current_count(), 0);
  EXPECT_EQ(orderer.get_current_index(), 5);
  contained = orderer.get_contained();
  ASSERT_EQ(contained.size(), 1);
  EXPECT_EQ(contained[0], std::make_pair(0ul, 5ul));  // Final range from 0 to 5
}
