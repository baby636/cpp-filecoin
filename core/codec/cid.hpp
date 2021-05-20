/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "cbor_blake/cid.hpp"
#include "codec/cbor/token.hpp"
#include "codec/uvarint.hpp"

namespace fc::codec::cid {
  constexpr std::array<uint8_t, 36> kMainnetGenesisParent{
      0x01, 0x71, 0x12, 0x20, 0x10, 0x7D, 0x82, 0x1C, 0x25, 0xDC, 0x07, 0x35,
      0x20, 0x02, 0x49, 0xDF, 0x94, 0xA8, 0xBE, 0xBC, 0x9C, 0x8E, 0x48, 0x97,
      0x44, 0xF8, 0x6A, 0x4C, 0xA8, 0x91, 0x9E, 0x81, 0xF1, 0x9D, 0xCD, 0x72,
  };

  constexpr std::array<uint8_t, 6> kCborBlakePrefix{
      0x01, 0x71, 0xA0, 0xE4, 0x02, 0x20};

  constexpr std::array<uint8_t, 3> kRawIdPrefix{0x01, 0x55, 0x00};

  inline bool readCborBlake(CbCidPtr &key, BytesIn &input) {
    if (readPrefix(input, kCborBlakePrefix) && readT(key, input)) {
      return true;
    }
    key = nullptr;
    return false;
  }

  inline bool readRawId(BytesIn &key, BytesIn &input) {
    key = {};
    return readPrefix(input, kRawIdPrefix) && uvarint::readBytes(key, input);
  }
}  // namespace fc::codec::cid

namespace fc::codec::cbor {
  inline bool readCborBlake(CbCidPtr &key,
                            const CborToken &token,
                            BytesIn &input) {
    BytesIn cid;
    return token.cidSize() && fc::read(cid, input, *token.cidSize())
           && cid::readCborBlake(key, cid) && cid.empty();
  }

  inline bool readCborBlake(CbCidPtr &key, BytesIn &input) {
    CborToken token;
    return read(token, input) && readCborBlake(key, token, input);
  }
}  // namespace fc::codec::cbor
