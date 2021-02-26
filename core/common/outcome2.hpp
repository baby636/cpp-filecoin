/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <spdlog/fmt/fmt.h>

#include "common/enum.hpp"
#include "common/outcome.hpp"

namespace fc {
  template <typename E>
  inline auto enumStr(const E &e) {
    return std::string{typeid(decltype(e)).name()} + ":"
           + std::to_string(common::to_int(e));
  }

  enum class OutcomeError { kDefault = 1 };

  template <typename T>
  struct Outcome : outcome::result<T> {
    using O = outcome::result<T>;
    Outcome() : O{outcome::failure(OutcomeError::kDefault)} {}
    template <typename... A>
    Outcome(A &&...a) : O{std::forward<A>(a)...} {}
    const T &operator*() const & {
      return O::value();
    }
    T &&operator*() && {
      return std::move(O::value());
    }
    T *operator->() {
      return &O::value();
    }
    operator const T &() const & {
      return O::value();
    }
    operator T &&() && {
      return std::move(O::value());
    }
    const auto &operator~() const {
      return O::error();
    }
    template <typename... A>
    void emplace(A &&...a) {
      *this = T{std::forward<A>(a)...};
    }
    O &&o() && {
      return std::move(*this);
    }
  };

  template <typename F>
  Outcome<std::invoke_result_t<F>> outcomeCatch(const F &f) {
    try {
      return f();
    } catch (std::system_error &e) {
      return outcome::failure(e.code());
    }
  }
}  // namespace fc

/**
 * std::error_code error;
 * fmt::format("... {}", error); // "... CATEGORY:VALUE"
 * fmt::format("... {:#}", error); // "... CATEGORY error VALUE \"MESSAGE\""
 */
template <>
struct fmt::formatter<std::error_code, char, void> {
  bool alt{false};

  template <typename ParseContext>
  constexpr auto parse(ParseContext &ctx) {
    auto it{ctx.begin()};
    if (it != ctx.end() && *it == '#') {
      alt = true;
      ++it;
    }
    return it;
  }

  template <typename FormatContext>
  auto format(const std::error_code &e, FormatContext &ctx) {
    if (alt) {
      return fmt::format_to(ctx.out(),
                            "{} error {}: \"{}\"",
                            e.category().name(),
                            e.value(),
                            e.message());
    }
    return fmt::format_to(ctx.out(), "{}:{}", e.category().name(), e.value());
  }
};

OUTCOME_HPP_DECLARE_ERROR(fc, OutcomeError);
