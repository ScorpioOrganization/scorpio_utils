#include "threading/waitable.hpp"

#include <algorithm>
#include "threading/wait_group.hpp"

void scorpio_utils::threading::WaitableBase::wait_with(WaitGroup* const wait_group) {
  if (_done) {
    wait_group->task_done();
  } else {
    _wait_groups.push_back(wait_group);
  }
}

void scorpio_utils::threading::WaitableBase::remove_wait_group(WaitGroup* wait_group) {
  _wait_groups.erase(std::remove(_wait_groups.begin(), _wait_groups.end(), wait_group), _wait_groups.end());
}

void scorpio_utils::threading::WaitableBase::done() {
  std::unique_lock<std::mutex> lock(_mutex);
  _done = true;
  for (auto _wait_group : _wait_groups) {
    std::lock_guard<std::mutex> lock(_wait_group->_mutex);
    _wait_group->task_done();
  }
  _wait_groups.clear();
}
