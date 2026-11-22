#pragma once

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "scorpio_utils/network/scorpio_udp.hpp"

// Names of the threads of this process that ScorpioUdp started (sudp_recv, sudp_send, sudp_proc, sudp_conn)
inline std::vector<std::string> scorpio_udp_thread_names() {
  std::vector<std::string> names;
  for (const auto& task : std::filesystem::directory_iterator("/proc/self/task")) {
    std::ifstream comm(task.path() / "comm");
    std::string name;
    if (std::getline(comm, name) && name.rfind("sudp_", 0) == 0) {
      names.push_back(std::move(name));
    }
  }
  return names;
}

// ScorpioUdp winds down in the background: a connection lets go of its ScorpioUdp only once its thread has seen the
// peer leave or time out, a moment after the test that used it returned. A test program that exits meanwhile lets
// those threads use what exit() already destroyed (the mock UDP network, static objects of the library), so it waits
// for them first, and fails if they never stop (a ScorpioUdp that leaked). Register it once per test program:
//   [[maybe_unused]] static auto* const environment =
//     ::testing::AddGlobalTestEnvironment(new WaitForScorpioUdpThreads());
class WaitForScorpioUdpThreads final : public ::testing::Environment {
public:
  void TearDown() override {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::nanoseconds(3 * SCU_UDP_TIMEOUT);
    auto names = scorpio_udp_thread_names();
    while (!names.empty() && std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      names = scorpio_udp_thread_names();
    }
    if (!names.empty()) {
      std::string list;
      for (const auto& name : names) {
        list += (list.empty() ? "" : ", ") + name;
      }
      ADD_FAILURE() << names.size() << " ScorpioUdp threads still running after every test ended: " << list;
    }
  }
};
