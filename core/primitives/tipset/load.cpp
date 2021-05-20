/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "primitives/tipset/load.hpp"

namespace fc::primitives::tipset {
  outcome::result<TipsetCPtr> TsLoad::loadw(TsWeak &weak,
                                            const TipsetKey &key) {
    if (auto ts{weak.lock()}) {
      return ts;
    }
    weak.reset();
    OUTCOME_TRY(ts, load(key));
    weak = ts;
    return std::move(ts);
  }

  outcome::result<TipsetCPtr> TsLoad::loadw(TsLazy &lazy) {
    return loadw(lazy.weak, lazy.key);
  }

  outcome::result<TipsetCPtr> TsLoad::load(std::vector<BlockHeader> blocks) {
    return Tipset::create(std::move(blocks));
  }

  TsLoadIpld::TsLoadIpld(IpldPtr ipld) : ipld{ipld} {}

  outcome::result<TipsetCPtr> TsLoadIpld::load(const TipsetKey &key) {
    std::vector<BlockHeader> blocks;
    blocks.reserve(key.cids().size());
    for (auto &cid : key.cids()) {
      OUTCOME_TRY(block, ipld->getCbor<BlockHeader>(cid));
      blocks.emplace_back(std::move(block));
    }
    return TsLoad::load(blocks);
  }

  TsLoadCache::TsLoadCache(TsLoadPtr ts_load, size_t cache_size)
      : ts_load{ts_load}, cache{cache_size} {}

  outcome::result<TipsetCPtr> TsLoadCache::load(const TipsetKey &key) {
    std::unique_lock lock{mutex};
    if (auto ts{cache.get(key)}) {
      return *ts;
    }
    lock.unlock();
    OUTCOME_TRY(ts, ts_load->load(key));
    lock.lock();
    cache.insert(key, ts);
    return std::move(ts);
  }

  outcome::result<TipsetCPtr> TsLoadCache::load(
      std::vector<BlockHeader> blocks) {
    OUTCOME_TRY(ts, ts_load->load(blocks));
    std::unique_lock lock{mutex};
    cache.insert(ts->key, ts);
    return std::move(ts);
  }

  CID put(const IpldPtr &ipld,
          const std::shared_ptr<PutBlockHeader> &put,
          const BlockHeader &header) {
    auto value{codec::cbor::encode(header).value()};
    auto key{crypto::blake2b::blake2b_256(value)};
    auto cid{asCborBlakeCid(key)};
    if (put) {
      put->put(key, value);
    } else {
      ipld->set(cid, std::move(value)).value();
    }
    return cid;
  }
}  // namespace fc::primitives::tipset
