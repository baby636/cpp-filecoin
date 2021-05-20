/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <string_view>

#include "codec/cid.hpp"

namespace fc::codec::actor {
  inline bool readActor(std::string_view &code, CbCidPtr &head, BytesIn input) {
    cbor::CborToken token;
    if (read(token, input).listCount() != 4) {
      return false;
    }
    BytesIn _cid;
    if (!read(token, input).cidSize() || !read(_cid, input, *token.cidSize())) {
      return false;
    }
    BytesIn _code;
    if (!cid::readRawId(_code, _cid) || !_cid.empty()) {
      return false;
    }
    code = common::span::bytestr(_code);
    return cbor::readCborBlake(head, input);
  }
}  // namespace fc::codec::actor
