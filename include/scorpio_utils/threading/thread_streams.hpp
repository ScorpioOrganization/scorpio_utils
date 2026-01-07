#pragma once

#include <iostream>
#include <memory>
#include <mutex>
#include <type_traits>
#include <utility>
#include "scorpio_utils/decorators.hpp"
#include "scorpio_utils/type_traits.hpp"

namespace scorpio_utils::threading {
template<typename T>
class ThreadSafeOStream : public std::enable_shared_from_this<ThreadSafeOStream<T>> {
public:
  using stream_type = std::decay_t<T>;
  class ThreadSafeOstreamGuard {
    friend class ThreadSafeOStream;
    std::shared_ptr<ThreadSafeOStream> _ts_os;

    explicit SCU_ALWAYS_INLINE ThreadSafeOstreamGuard(std::shared_ptr<ThreadSafeOStream> ts_os)
    : _ts_os(std::move(ts_os)) { }
    ThreadSafeOstreamGuard(const ThreadSafeOstreamGuard&) = delete;
    ThreadSafeOstreamGuard(ThreadSafeOstreamGuard&&) = default;
    ThreadSafeOstreamGuard& operator=(const ThreadSafeOstreamGuard&) = delete;
    ThreadSafeOstreamGuard& operator=(ThreadSafeOstreamGuard&&) = delete;

public:
    SCU_ALWAYS_INLINE ~ThreadSafeOstreamGuard() {
      _ts_os->_mutex.unlock();
    }

    template<typename Y>
    ThreadSafeOstreamGuard& operator<<(Y&& value) {
      static_assert(HasBitShiftLeft<stream_type, Y>::value,
        "The type T must support the << operator with the type Y");
      _ts_os->_os << std::forward<Y>(value);
      return *this;
    }
  };
  friend class ThreadSafeOstreamGuard;

private:
  stream_type& _os;
  std::mutex _mutex;

  SCU_ALWAYS_INLINE ThreadSafeOStream(stream_type& os)  // NOLINT
  : _os(os) { }
  ThreadSafeOStream(const ThreadSafeOStream&) = delete;
  ThreadSafeOStream& operator=(const ThreadSafeOStream&) = delete;
  ThreadSafeOStream(ThreadSafeOStream&&) = delete;
  ThreadSafeOStream& operator=(ThreadSafeOStream&&) = delete;

public:
  SCU_ALWAYS_INLINE ~ThreadSafeOStream() = default;
  SCU_ALWAYS_INLINE static auto create(stream_type& ios) {
    return std::shared_ptr<ThreadSafeOStream>(new ThreadSafeOStream(ios));
  }

  template<typename Y>
  ThreadSafeOstreamGuard operator<<(Y&& value) {
    static_assert(HasBitShiftLeft<stream_type, Y>::value,
      "The type T must support the << operator with the type Y");
    _mutex.lock();
    _os << std::forward<Y>(value);
    return ThreadSafeOstreamGuard(this->shared_from_this());
  }
};

template<typename S, typename T>
SCU_ALWAYS_INLINE auto operator<<(std::shared_ptr<ThreadSafeOStream<S>> ts_os, T&& value) {
  return (*ts_os) << std::forward<T>(value);
}

template<typename T>
class ThreadSafeIStream : public std::enable_shared_from_this<ThreadSafeIStream<T>> {
public:
  using stream_type = std::decay_t<T>;
  class ThreadSafeIstreamGuard {
    friend class ThreadSafeIStream;
    std::shared_ptr<ThreadSafeIStream> _ts_is;

    explicit SCU_ALWAYS_INLINE ThreadSafeIstreamGuard(std::shared_ptr<ThreadSafeIStream> ts_is)
    : _ts_is(std::move(ts_is)) { }
    ThreadSafeIstreamGuard(const ThreadSafeIstreamGuard&) = delete;
    ThreadSafeIstreamGuard(ThreadSafeIstreamGuard&&) = delete;
    ThreadSafeIstreamGuard& operator=(const ThreadSafeIstreamGuard&) = delete;
    ThreadSafeIstreamGuard& operator=(ThreadSafeIstreamGuard&&) = delete;

public:
    SCU_ALWAYS_INLINE ~ThreadSafeIstreamGuard() {
      _ts_is->_mutex.unlock();
    }

    template<typename Y>
    ThreadSafeIstreamGuard& operator>>(Y&& value) {
      static_assert(HasBitShiftRight<stream_type, Y>::value,
        "The type T must support the >> operator with the type Y");
      _ts_is->_is >> std::forward<Y>(value);
      return *this;
    }
  };
  friend class ThreadSafeIstreamGuard;

private:
  stream_type& _is;
  std::mutex _mutex;

  SCU_ALWAYS_INLINE ThreadSafeIStream(stream_type& is)  // NOLINT
  : _is(is) { }
  ThreadSafeIStream(const ThreadSafeIStream&) = delete;
  ThreadSafeIStream& operator=(const ThreadSafeIStream&) = delete;
  ThreadSafeIStream(ThreadSafeIStream&&) = delete;
  ThreadSafeIStream& operator=(ThreadSafeIStream&&) = delete;

public:
  SCU_ALWAYS_INLINE ~ThreadSafeIStream() = default;
  SCU_ALWAYS_INLINE static auto create(stream_type& ios) {
    return std::shared_ptr<ThreadSafeIStream>(new ThreadSafeIStream(ios));
  }

  template<typename Y>
  ThreadSafeIstreamGuard operator>>(Y&& value) {
    static_assert(HasBitShiftRight<stream_type, Y>::value,
      "The type T must support the >> operator with the type Y");
    _mutex.lock();
    _is >> std::forward<Y>(value);
    return ThreadSafeIstreamGuard(this->shared_from_this());
  }
};

template<typename S, typename T>
SCU_ALWAYS_INLINE auto operator>>(std::shared_ptr<ThreadSafeIStream<S>> ts_is, T&& value) {
  return (*ts_is) >> std::forward<T>(value);
}

template<typename T>
class ThreadSafeIOStream : public std::enable_shared_from_this<ThreadSafeIOStream<T>> {
public:
  using stream_type = std::decay_t<T>;
  class ThreadSafeIOstreamGuard {
    friend class ThreadSafeIOStream;
    std::shared_ptr<ThreadSafeIOStream> _ts_ios;

    explicit SCU_ALWAYS_INLINE ThreadSafeIOstreamGuard(std::shared_ptr<ThreadSafeIOStream> ts_ios)
    : _ts_ios(std::move(ts_ios)) { }
    ThreadSafeIOstreamGuard(const ThreadSafeIOstreamGuard&) = delete;
    ThreadSafeIOstreamGuard(ThreadSafeIOstreamGuard&&) = default;
    ThreadSafeIOstreamGuard& operator=(const ThreadSafeIOstreamGuard&) = delete;
    ThreadSafeIOstreamGuard& operator=(ThreadSafeIOstreamGuard&&) = delete;

public:
    SCU_ALWAYS_INLINE ~ThreadSafeIOstreamGuard() {
      _ts_ios->_mutex.unlock();
    }

    template<typename Y>
    ThreadSafeIOstreamGuard& operator>>(Y&& value) {
      static_assert(HasBitShiftRight<stream_type, Y>::value,
        "The type T must support the >> operator with the type Y");
      _ts_ios->_ios >> std::forward<Y>(value);
      return *this;
    }

    template<typename Y>
    ThreadSafeIOstreamGuard& operator<<(Y&& value) {
      static_assert(HasBitShiftLeft<stream_type, Y>::value,
        "The type T must support the << operator with the type Y");
      _ts_ios->_ios << std::forward<Y>(value);
      return *this;
    }
  };
  friend class ThreadSafeIOstreamGuard;

private:
  stream_type& _ios;
  std::mutex _mutex;

  SCU_ALWAYS_INLINE ThreadSafeIOStream(stream_type& ios)  // NOLINT
  : _ios(ios) { }
  ThreadSafeIOStream(const ThreadSafeIOStream&) = delete;
  ThreadSafeIOStream& operator=(const ThreadSafeIOStream&) = delete;
  ThreadSafeIOStream(ThreadSafeIOStream&&) = delete;
  ThreadSafeIOStream& operator=(ThreadSafeIOStream&&) = delete;

public:
  SCU_ALWAYS_INLINE ~ThreadSafeIOStream() = default;
  SCU_ALWAYS_INLINE static auto create(stream_type& ios) {
    return std::shared_ptr<ThreadSafeIOStream>(new ThreadSafeIOStream(ios));
  }

  template<typename Y>
  ThreadSafeIOstreamGuard operator<<(Y&& value) {
    static_assert(HasBitShiftLeft<stream_type, Y>::value,
      "The type T must support the << operator with the type Y");
    _mutex.lock();
    _ios << std::forward<Y>(value);
    return ThreadSafeIOstreamGuard(this->shared_from_this());
  }

  template<typename Y>
  ThreadSafeIOstreamGuard operator>>(Y&& value) {
    static_assert(HasBitShiftRight<stream_type, Y>::value,
      "The type T must support the >> operator with the type Y");
    _mutex.lock();
    _ios >> std::forward<Y>(value);
    return ThreadSafeIOstreamGuard(this->shared_from_this());
  }
};

template<typename S, typename T>
auto operator<<(std::shared_ptr<ThreadSafeIOStream<S>> ts_os, T&& value) {
  return (*ts_os) << std::forward<T>(value);
}

template<typename S, typename T>
SCU_ALWAYS_INLINE auto operator>>(std::shared_ptr<ThreadSafeIOStream<S>> ts_is, T&& value) {
  return (*ts_is) >> std::forward<T>(value);
}

static std::shared_ptr<ThreadSafeOStream<std::add_lvalue_reference_t<decltype(std::cout)>>> ts_cout;
static std::shared_ptr<ThreadSafeOStream<std::add_lvalue_reference_t<decltype(std::cerr)>>> ts_cerr;
static std::shared_ptr<ThreadSafeIStream<std::add_lvalue_reference_t<decltype(std::cin)>>> ts_cin;
}  // namespace scorpio_utils::threading
