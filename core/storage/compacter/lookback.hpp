/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "codec/actor.hpp"
#include "codec/address.hpp"
#include "codec/hamt.hpp"
#include "storage/compacter/queue.hpp"
#include "vm/actor/codes.hpp"

// TODO: keep own miner sectors
namespace fc::storage::compacter {
  using vm::actor::code::Code;

  constexpr bool anyOf(
      Code code, Code code0, Code code2, Code code3, Code code4) {
    return code == code0 || code == code2 || code == code3 || code == code4;
  }

  inline void lookbackActor(std::vector<CbCid> &copy,
                            std::vector<CbCid> &recurse,
                            const CbIpldPtr &ipld,
                            std::string_view code,
                            const CbCid &head) {
    using namespace vm::actor::code;
    if (anyOf(code, account0, account2, account3, account4)) {
      copy.push_back(head);
      return;
    }
    if (anyOf(code, init0, init2, init3, init4)) {
      recurse.push_back(head);
      return;
    }
    if (anyOf(code, miner0, miner2, miner3, miner4)) {
      // TODO
    }
    if (anyOf(code, power0, power2, power3, power4)) {
      // TODO
    }
  }

  inline void lookbackActors(std::vector<CbCid> &copy,
                             std::vector<CbCid> &recurse,
                             const CbIpldPtr &ipld,
                             const CbIpldPtr &visited,
                             const CbCid &state) {
    CbCid _hamt;
    uint64_t version{};
    if (!codec::hamt::stateTree(_hamt, version, ipld, state)) {
      return;
    }
    copy.push_back(state);
    copy.push_back(_hamt);
    codec::hamt::HamtWalk hamt{ipld, _hamt};
    hamt.visited = visited;
    BytesIn _addr, _actor;
    while (hamt.next(_addr, _actor)) {
      uint64_t id;
      std::string_view code;
      CbCidPtr head;
      if (!codec::address::readId(id, _addr)) {
        throw std::logic_error{"lookbackActors readId"};
      }
      if (!codec::actor::readActor(code, head, _actor)) {
        throw std::logic_error{"lookbackActors readActor"};
      }
      if (!visited->has(*head)) {
        lookbackActor(copy, recurse, ipld, code, *head);
      }
    }
    copy.insert(copy.begin(), hamt.cids.begin(), hamt.cids.end());
    return;
  }
}  // namespace fc::storage::compacter
