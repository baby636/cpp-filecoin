/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "primitives/cid/cid.hpp"
#include "primitives/rle_bitset/rle_bitset.hpp"
#include "primitives/sector/sector.hpp"
#include "primitives/types.hpp"

namespace fc::vm::actor::builtin::types::miner {
  using primitives::ChainEpoch;
  using primitives::DealId;
  using primitives::DealWeight;
  using primitives::RleBitset;
  using primitives::SectorNumber;
  using primitives::TokenAmount;
  using primitives::sector::RegisteredSealProof;

  struct SectorOnChainInfo {
    SectorNumber sector{};
    RegisteredSealProof seal_proof;
    CID sealed_cid;
    std::vector<DealId> deals;
    ChainEpoch activation_epoch{};
    ChainEpoch expiration{};
    DealWeight deal_weight{};
    DealWeight verified_deal_weight{};
    TokenAmount init_pledge{};
    TokenAmount expected_day_reward{};
    TokenAmount expected_storage_pledge{};
  };
  CBOR_TUPLE(SectorOnChainInfo,
             sector,
             seal_proof,
             sealed_cid,
             deals,
             activation_epoch,
             expiration,
             deal_weight,
             verified_deal_weight,
             init_pledge,
             expected_day_reward,
             expected_storage_pledge)

  /**
   * Type used in actor method parameters
   */
  struct SectorDeclaration {
    /**
     * The deadline to which the sectors are assigned, in range
     * [0..WPoStPeriodDeadlines)
     */
    uint64_t deadline{0};

    /** Partition index within the deadline containing the sectors. */
    uint64_t partition{0};

    /** Sectors in the partition being declared faulty. */
    RleBitset sectors;
  };
  CBOR_TUPLE(SectorDeclaration, deadline, partition, sectors)

  struct SectorPreCommitInfo {
    RegisteredSealProof registered_proof;
    SectorNumber sector{};
    /// CommR
    CID sealed_cid;
    ChainEpoch seal_epoch{};
    std::vector<DealId> deal_ids;
    /// Sector expiration
    ChainEpoch expiration{};
    bool replace_capacity = false;
    uint64_t replace_deadline{};
    uint64_t replace_partition{};
    SectorNumber replace_sector{};
  };
  CBOR_TUPLE(SectorPreCommitInfo,
             registered_proof,
             sector,
             sealed_cid,
             seal_epoch,
             deal_ids,
             expiration,
             replace_capacity,
             replace_deadline,
             replace_partition,
             replace_sector)

  struct SectorPreCommitOnChainInfo {
    SectorPreCommitInfo info;
    TokenAmount precommit_deposit{};
    ChainEpoch precommit_epoch{};
    DealWeight deal_weight{};
    DealWeight verified_deal_weight{};
  };
  CBOR_TUPLE(SectorPreCommitOnChainInfo,
             info,
             precommit_deposit,
             precommit_epoch,
             deal_weight,
             verified_deal_weight)

}  // namespace fc::vm::actor::builtin::types::miner
