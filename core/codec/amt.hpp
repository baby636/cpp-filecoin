/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "codec/_tree.hpp"
#include "codec/cbor/token.hpp"
#include "codec/cid.hpp"

namespace fc::codec::amt {
  struct AmtWalk : _tree::Tree {
    using Tree::Tree;

    size_t _values{};

    bool _readNode() {
      cbor::CborToken token;
      if (read(token, node).listCount() != 3) {
        return false;
      }
      if (!read(token, node).bytesSize() || !read(node, *token.bytesSize())) {
        return false;
      }
      if (!read(token, node).listCount()) {
        return false;
      }
      for (auto links{*token.listCount()}; links; --links) {
        CbCidPtr cid;
        if (!cbor::readCborBlake(cid, node)) {
          return false;
        }
        _push(*cid);
      }
      if (!read(token, node).listCount()) {
        return false;
      }
      _values = *token.listCount();
      return true;
    }

    bool load() {
      cbor::CborToken token;
      if (!_next()) {
        return false;
      }
      if (!read(token, node).listCount()) {
        return false;
      }
      if (token.listCount() == 4) {
        if (!read(token, node).asUint()) {
          return false;
        }
      } else if (token.listCount() != 3) {
        return false;
      }
      if (!read(token, node).asUint()) {
        return false;
      }
      if (!read(token, node).asUint()) {
        return false;
      }
      if (!_readNode()) {
        return false;
      }
      return true;
    }

    bool next(BytesIn &value) {
      while (!empty()) {
        if (_values) {
          --_values;
          if (!codec::cbor::readNested(value, node)) {
            return false;
          }
          return true;
        } else {
          if (!node.empty()) {
            return false;
          }
          if (_next() && !_readNode()) {
            return false;
          }
        }
      }
      return false;
    }
  };

  inline bool msgMeta(CbCidPtr &bls,
                      CbCidPtr &secp,
                      CbIpldPtr ipld,
                      const CbCid &cid) {
    Buffer value;
    if (!ipld->get(cid, value)) {
      return false;
    }
    BytesIn input{value};
    cbor::CborToken token;
    return read(token, input).listCount() == 2
           && cbor::readCborBlake(bls, input)
           && cbor::readCborBlake(secp, input);
  }
}  // namespace fc::codec::amt
