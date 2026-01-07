#include "scorpio_utils/threading/thread_streams.hpp"

using scorpio_utils::threading::ThreadSafeIStream;
using scorpio_utils::threading::ThreadSafeIOStream;
using scorpio_utils::threading::ThreadSafeOStream;

std::shared_ptr<ThreadSafeOStream<std::add_lvalue_reference_t<decltype(std::cout)>>> ts_cout =
  ThreadSafeOStream<std::add_lvalue_reference_t<decltype(std::cout)>>::create(std::cout);
std::shared_ptr<ThreadSafeOStream<std::add_lvalue_reference_t<decltype(std::cerr)>>> ts_cerr =
  ThreadSafeOStream<std::add_lvalue_reference_t<decltype(std::cerr)>>::create(std::cerr);
std::shared_ptr<ThreadSafeIStream<std::add_lvalue_reference_t<decltype(std::cin)>>> ts_cin =
  ThreadSafeIStream<std::add_lvalue_reference_t<decltype(std::cin)>>::create(std::cin);
