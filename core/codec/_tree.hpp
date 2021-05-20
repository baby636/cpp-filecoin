/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "cbor_blake/ipld.hpp"

namespace fc::codec::_tree {
  struct Tree {
    CbIpldPtr ipld;
    std::vector<CbCid> cids;
    size_t next_cid{};
    Buffer _node;
    BytesIn node;
    CbIpldPtr visited;

    Tree(CbIpldPtr ipld, CbCid root) : ipld{ipld} {
      cids.push_back(root);
    }

    bool empty() const {
      return node.empty() && next_cid == cids.size();
    }

    bool _next() {
      assert(!empty());
      if (ipld->get(cids[next_cid++], _node)) {
        node = _node;
        return true;
      }
      _node.resize(0);
      node = {};
      return false;
    }

    void _push(const CbCid &cid) {
      if (!visited || !visited->has(cid)) {
        cids.push_back(cid);
      }
    }
  };
}  // namespace fc::codec::_tree
