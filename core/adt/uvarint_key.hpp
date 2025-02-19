/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <string>

#include "common/outcome.hpp"

namespace fc::adt {
  enum class UvarintKeyError { kDecodeError = 1 };

  struct UvarintKeyer {
    using Key = uint64_t;

    static std::string encode(Key key);

    static outcome::result<Key> decode(const std::string &key);
  };

  struct VarintKeyer {
    using Key = int64_t;

    static std::string encode(Key key);

    static outcome::result<Key> decode(const std::string &key);
  };
}  // namespace fc::adt

OUTCOME_HPP_DECLARE_ERROR(fc::adt, UvarintKeyError);
