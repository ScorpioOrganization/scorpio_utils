#pragma once

#include <mutex>
#include <vector>

namespace scorpio_utils::threading {
class WaitGroup;
class WaitableBase {
  friend class WaitGroup;

  std::vector<WaitGroup*> _wait_groups;
  std::mutex _mutex;
  bool _done;
  void wait_with(WaitGroup* wait_group);
  void remove_wait_group(WaitGroup* wait_group);

protected:
  void done();
  WaitableBase()
  : _done(false) { }
  WaitableBase(const WaitableBase&) = delete;
  WaitableBase& operator=(const WaitableBase&) = delete;

public:
  virtual ~WaitableBase() = default;
};
}  // namespace scorpio_utils::threading
