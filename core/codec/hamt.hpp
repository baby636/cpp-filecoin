/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "codec/_tree.hpp"
#include "codec/cbor/token.hpp"
#include "codec/cid.hpp"

namespace fc::codec::hamt {
  struct HamtWalk : _tree::Tree {
    using Tree::Tree;

    size_t _bucket{};

    bool next(BytesIn &key, BytesIn &value) {
      cbor::CborToken token;
      while (!empty()) {
        if (_bucket) {
          --_bucket;
          if (read(token, node).listCount() != 2) {
            return false;
          }
          if (!read(token, node).bytesSize()) {
            return false;
          }
          if (!read(key, node, *token.bytesSize())) {
            return false;
          }
          if (!codec::cbor::readNested(value, node)) {
            return false;
          }
          return true;
        } else if (node.empty()) {
          if (_next()) {
            if (read(token, node).listCount() != 2) {
              return false;
            }
            if (!read(token, node).bytesSize()
                || !read(node, *token.bytesSize())) {
              return false;
            }
            if (!read(token, node).listCount()) {
              return false;
            }
          }
        } else {
          if (!read(token, node)) {
            return false;
          }
          if (token.mapCount()) {
            if (token.mapCount() != 1) {
              return false;
            }
            if (read(token, node).strSize() != 1) {
              return false;
            }
            BytesIn str;
            if (!read(str, node, 1) || (str[0] != '0' && str[0] != '1')) {
              return false;
            }
            if (!read(token, node)) {
              return false;
            }
          }
          if (token.cidSize()) {
            CbCidPtr cid;
            if (!readCborBlake(cid, token, node)) {
              return false;
            }
            _push(*cid);
          } else {
            if (!token.listCount()) {
              return false;
            }
            _bucket = *token.listCount();
          }
        }
      }
      return false;
    }
  };

  inline bool stateTree(CbCid &hamt,
                        uint64_t version,
                        CbIpldPtr ipld,
                        const CbCid &root) {
    hamt = root;
    version = 0;
    Buffer value;
    if (!ipld->get(root, value)) {
      return false;
    }
    BytesIn input{value};
    cbor::CborToken token;
    if (!read(token, input).listCount()) {
      return false;
    }
    if (token.listCount() == 3) {
      if (!read(token, input).asUint()) {
        return false;
      }
      version = *token.asUint();
      CbCidPtr cid;
      if (!cbor::readCborBlake(cid, input)) {
        return false;
      }
      hamt = *cid;
    }
    return true;
  }
}  // namespace fc::codec::hamt
