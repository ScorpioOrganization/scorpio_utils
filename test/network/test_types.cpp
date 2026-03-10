#include <gtest/gtest.h>

#include "scorpio_utils/network/types.hpp"

using scorpio_utils::network::host_to_network;
using scorpio_utils::network::network_to_host;
using scorpio_utils::network::is_big_endian;

// Test fixture for network byte order conversion tests
class NetworkTypesTest : public ::testing::Test {
protected:
  void SetUp() override { }
  void TearDown() override { }
};

// Test endianness detection
TEST_F(NetworkTypesTest, EndiannessDetection) {
  // Just verify that the endianness detection doesn't crash and returns a boolean
  EXPECT_TRUE(is_big_endian == true || is_big_endian == false);
}

// Test network_to_host and host_to_network for uint16_t
TEST_F(NetworkTypesTest, ConvertUint16) {
  uint16_t host_value = 0x1234;
  uint16_t network_value = 0;
  uint16_t back_to_host = 0;

  // Convert host to network
  host_to_network(&host_value, &network_value, sizeof(uint16_t));

  // Convert network back to host
  network_to_host(&network_value, &back_to_host, sizeof(uint16_t));

  // Should get the original value back
  EXPECT_EQ(host_value, back_to_host);

  // On little-endian systems, bytes should be swapped
  if (!is_big_endian) {
    uint8_t* net_bytes = reinterpret_cast<uint8_t*>(&network_value);
    EXPECT_EQ(net_bytes[0], 0x12);
    EXPECT_EQ(net_bytes[1], 0x34);
  }
}

// Test network_to_host and host_to_network for uint32_t
TEST_F(NetworkTypesTest, ConvertUint32) {
  uint32_t host_value = 0x12345678;
  uint32_t network_value = 0;
  uint32_t back_to_host = 0;

  // Convert host to network
  host_to_network(&host_value, &network_value, sizeof(uint32_t));

  // Convert network back to host
  network_to_host(&network_value, &back_to_host, sizeof(uint32_t));

  // Should get the original value back
  EXPECT_EQ(host_value, back_to_host);

  // On little-endian systems, bytes should be swapped
  if (!is_big_endian) {
    uint8_t* net_bytes = reinterpret_cast<uint8_t*>(&network_value);
    EXPECT_EQ(net_bytes[0], 0x12);
    EXPECT_EQ(net_bytes[1], 0x34);
    EXPECT_EQ(net_bytes[2], 0x56);
    EXPECT_EQ(net_bytes[3], 0x78);
  }
}

// Test network_to_host and host_to_network for uint64_t
TEST_F(NetworkTypesTest, ConvertUint64) {
  uint64_t host_value = 0x123456789ABCDEF0;
  uint64_t network_value = 0;
  uint64_t back_to_host = 0;

  // Convert host to network
  host_to_network(&host_value, &network_value, sizeof(uint64_t));

  // Convert network back to host
  network_to_host(&network_value, &back_to_host, sizeof(uint64_t));

  // Should get the original value back
  EXPECT_EQ(host_value, back_to_host);

  // On little-endian systems, bytes should be swapped
  if (!is_big_endian) {
    uint8_t* net_bytes = reinterpret_cast<uint8_t*>(&network_value);
    EXPECT_EQ(net_bytes[0], 0x12);
    EXPECT_EQ(net_bytes[1], 0x34);
    EXPECT_EQ(net_bytes[2], 0x56);
    EXPECT_EQ(net_bytes[3], 0x78);
    EXPECT_EQ(net_bytes[4], 0x9A);
    EXPECT_EQ(net_bytes[5], 0xBC);
    EXPECT_EQ(net_bytes[6], 0xDE);
    EXPECT_EQ(net_bytes[7], 0xF0);
  }
}

// Test network_to_host and host_to_network for int16_t
TEST_F(NetworkTypesTest, ConvertInt16) {
  int16_t host_value = -12345;
  int16_t network_value = 0;
  int16_t back_to_host = 0;

  // Convert host to network
  host_to_network(&host_value, &network_value, sizeof(int16_t));

  // Convert network back to host
  network_to_host(&network_value, &back_to_host, sizeof(int16_t));

  // Should get the original value back
  EXPECT_EQ(host_value, back_to_host);
}

// Test network_to_host and host_to_network for int32_t
TEST_F(NetworkTypesTest, ConvertInt32) {
  int32_t host_value = -123456789;
  int32_t network_value = 0;
  int32_t back_to_host = 0;

  // Convert host to network
  host_to_network(&host_value, &network_value, sizeof(int32_t));

  // Convert network back to host
  network_to_host(&network_value, &back_to_host, sizeof(int32_t));

  // Should get the original value back
  EXPECT_EQ(host_value, back_to_host);
}

// Test network_to_host and host_to_network for float
TEST_F(NetworkTypesTest, ConvertFloat) {
  float host_value = 3.14159f;
  float network_value = 0.0f;
  float back_to_host = 0.0f;

  // Convert host to network
  host_to_network(&host_value, &network_value, sizeof(float));

  // Convert network back to host
  network_to_host(&network_value, &back_to_host, sizeof(float));

  // Should get the original value back
  EXPECT_FLOAT_EQ(host_value, back_to_host);
}

// Test network_to_host and host_to_network for double
TEST_F(NetworkTypesTest, ConvertDouble) {
  double host_value = 3.141592653589793;
  double network_value = 0.0;
  double back_to_host = 0.0;

  // Convert host to network
  host_to_network(&host_value, &network_value, sizeof(double));

  // Convert network back to host
  network_to_host(&network_value, &back_to_host, sizeof(double));

  // Should get the original value back
  EXPECT_DOUBLE_EQ(host_value, back_to_host);
}

// Test vector-based network_to_host with position tracking
TEST_F(NetworkTypesTest, VectorNetworkToHostSuccess) {
  std::vector<uint8_t> buffer = { 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0 };
  size_t pos = 0;

  uint16_t value16 = 0;
  EXPECT_TRUE(network_to_host(buffer, &value16, pos));
  EXPECT_EQ(pos, 2u);

  uint32_t value32 = 0;
  EXPECT_TRUE(network_to_host(buffer, &value32, pos));
  EXPECT_EQ(pos, 6u);

  uint16_t value16_2 = 0;
  EXPECT_TRUE(network_to_host(buffer, &value16_2, pos));
  EXPECT_EQ(pos, 8u);
}

// Test vector-based network_to_host with insufficient data
TEST_F(NetworkTypesTest, VectorNetworkToHostInsufficientData) {
  std::vector<uint8_t> buffer = { 0x12, 0x34 };
  size_t pos = 0;

  uint32_t value32 = 0;
  EXPECT_FALSE(network_to_host(buffer, &value32, pos));
  EXPECT_EQ(pos, 0u);  // Position should not advance on failure
}

// Test vector-based network_to_host at buffer boundary
TEST_F(NetworkTypesTest, VectorNetworkToHostBoundary) {
  std::vector<uint8_t> buffer = { 0x12, 0x34, 0x56, 0x78 };
  size_t pos = 2;

  uint16_t value16 = 0;
  EXPECT_TRUE(network_to_host(buffer, &value16, pos));
  EXPECT_EQ(pos, 4u);

  // Now try to read beyond buffer
  uint16_t value16_2 = 0;
  EXPECT_FALSE(network_to_host(buffer, &value16_2, pos));
  EXPECT_EQ(pos, 4u);  // Position should not advance on failure
}

// Test vector-based host_to_network with position tracking
TEST_F(NetworkTypesTest, VectorHostToNetworkSuccess) {
  std::vector<uint8_t> buffer(8);
  size_t pos = 0;

  uint16_t value16 = 0x1234;
  EXPECT_TRUE(host_to_network(value16, buffer, pos));
  EXPECT_EQ(pos, 2u);

  uint32_t value32 = 0x56789ABC;
  EXPECT_TRUE(host_to_network(value32, buffer, pos));
  EXPECT_EQ(pos, 6u);

  uint16_t value16_2 = 0xDEF0;
  EXPECT_TRUE(host_to_network(value16_2, buffer, pos));
  EXPECT_EQ(pos, 8u);

  // Read back to verify
  pos = 0;
  uint16_t read16 = 0;
  EXPECT_TRUE(network_to_host(buffer, &read16, pos));
  EXPECT_EQ(read16, value16);

  uint32_t read32 = 0;
  EXPECT_TRUE(network_to_host(buffer, &read32, pos));
  EXPECT_EQ(read32, value32);

  uint16_t read16_2 = 0;
  EXPECT_TRUE(network_to_host(buffer, &read16_2, pos));
  EXPECT_EQ(read16_2, value16_2);
}

// Test vector-based host_to_network with insufficient space
TEST_F(NetworkTypesTest, VectorHostToNetworkInsufficientSpace) {
  std::vector<uint8_t> buffer(2);
  size_t pos = 0;

  uint32_t value32 = 0x12345678;
  EXPECT_FALSE(host_to_network(value32, buffer, pos));
  EXPECT_EQ(pos, 0u);  // Position should not advance on failure
}

// Test vector-based host_to_network at buffer boundary
TEST_F(NetworkTypesTest, VectorHostToNetworkBoundary) {
  std::vector<uint8_t> buffer(4);
  size_t pos = 2;

  uint16_t value16 = 0x1234;
  EXPECT_TRUE(host_to_network(value16, buffer, pos));
  EXPECT_EQ(pos, 4u);

  // Now try to write beyond buffer
  uint16_t value16_2 = 0x5678;
  EXPECT_FALSE(host_to_network(value16_2, buffer, pos));
  EXPECT_EQ(pos, 4u);  // Position should not advance on failure
}

// Test template-based network_to_host (return value version)
TEST_F(NetworkTypesTest, TemplateNetworkToHostUint32) {
  uint32_t data = { 0x12345678u };
  uint32_t value = network_to_host<uint32_t>(&data);

  if (is_big_endian) {
    EXPECT_EQ(value, 0x12345678u);
  } else {
    // On little-endian, bytes should be reversed
    EXPECT_EQ(value, 0x78563412u);
  }
}

// Test template-based network_to_host with uint16_t
TEST_F(NetworkTypesTest, TemplateNetworkToHostUint16) {
  uint16_t data = 0x1234;
  uint16_t value = network_to_host<uint16_t>(&data);

  if (is_big_endian) {
    EXPECT_EQ(value, 0x1234u);
  } else {
    EXPECT_EQ(value, 0x3412u);
  }
}

// Test template-based network_to_host with uint64_t
TEST_F(NetworkTypesTest, TemplateNetworkToHostUint64) {
  uint64_t data = 0x123456789ABCDEF0;
  uint64_t value = network_to_host<uint64_t>(&data);

  if (is_big_endian) {
    EXPECT_EQ(value, 0x123456789ABCDEF0u);
  } else {
    EXPECT_EQ(value, 0xF0DEBC9A78563412u);
  }
}

// Test template-based host_to_network (return value version)
TEST_F(NetworkTypesTest, TemplateHostToNetworkUint32) {
  uint32_t data = 0x12345678;
  uint32_t value = host_to_network<uint32_t>(&data);

  if (is_big_endian) {
    EXPECT_EQ(value, 0x12345678u);
  } else {
    EXPECT_EQ(value, 0x78563412u);
  }
}

// Test with edge case values
TEST_F(NetworkTypesTest, EdgeCaseZero) {
  uint32_t host_value = 0;
  uint32_t network_value = 0;
  uint32_t back_to_host = 0;

  host_to_network(&host_value, &network_value, sizeof(uint32_t));
  network_to_host(&network_value, &back_to_host, sizeof(uint32_t));

  EXPECT_EQ(host_value, back_to_host);
}

// Test with edge case values
TEST_F(NetworkTypesTest, EdgeCaseMaxValue) {
  uint32_t host_value = 0xFFFFFFFF;
  uint32_t network_value = 0;
  uint32_t back_to_host = 0;

  host_to_network(&host_value, &network_value, sizeof(uint32_t));
  network_to_host(&network_value, &back_to_host, sizeof(uint32_t));

  EXPECT_EQ(host_value, back_to_host);
}

// Test round-trip conversion with multiple types in sequence
TEST_F(NetworkTypesTest, MixedTypesRoundTrip) {
  std::vector<uint8_t> buffer(20);
  size_t write_pos = 0;

  // Write different types
  uint16_t val16 = 0x1234;
  uint32_t val32 = 0x56789ABC;
  float valf = 42.42f;
  double vald = 123.456789;

  EXPECT_TRUE(host_to_network(val16, buffer, write_pos));
  EXPECT_TRUE(host_to_network(val32, buffer, write_pos));
  EXPECT_TRUE(host_to_network(valf, buffer, write_pos));
  EXPECT_TRUE(host_to_network(vald, buffer, write_pos));

  // Read them back
  size_t read_pos = 0;
  uint16_t read16 = 0;
  uint32_t read32 = 0;
  float readf = 0.0f;
  double readd = 0.0;

  EXPECT_TRUE(network_to_host(buffer, &read16, read_pos));
  EXPECT_TRUE(network_to_host(buffer, &read32, read_pos));
  EXPECT_TRUE(network_to_host(buffer, &readf, read_pos));
  EXPECT_TRUE(network_to_host(buffer, &readd, read_pos));

  EXPECT_EQ(val16, read16);
  EXPECT_EQ(val32, read32);
  EXPECT_FLOAT_EQ(valf, readf);
  EXPECT_DOUBLE_EQ(vald, readd);
}
