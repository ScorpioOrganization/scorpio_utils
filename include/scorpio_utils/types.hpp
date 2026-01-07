#pragma once

namespace scorpio_utils {
class Impossible final {
  Impossible() = delete;
  Impossible(const Impossible&) = delete;
  Impossible(Impossible&&) = delete;
  Impossible& operator=(const Impossible&) = delete;
  Impossible& operator=(Impossible&&) = delete;
};

class Success final {
public:
  constexpr Success() noexcept = default;
  constexpr Success(const Success&) noexcept = default;
  constexpr Success(Success&&) noexcept = default;
  constexpr Success& operator=(const Success&) noexcept = default;
  constexpr Success& operator=(Success&&) noexcept = default;

  static constexpr Success instance() noexcept {
    return Success();
  }

  constexpr bool operator==(const Success&) const noexcept {
    return true;
  }

  constexpr bool operator!=(const Success&) const noexcept {
    return false;
  }

  constexpr operator bool() const noexcept {
    return true;
  }
};

struct Empty { };

}  // namespace scorpio_utils
