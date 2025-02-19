/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "blockchain/impl/weight_calculator_impl.hpp"

#include "common/logger.hpp"
#include "vm/actor/builtin/states/state_provider.hpp"
#include "vm/state/impl/state_tree_impl.hpp"

OUTCOME_CPP_DEFINE_CATEGORY(fc::blockchain::weight, WeightCalculatorError, e) {
  using E = fc::blockchain::weight::WeightCalculatorError;
  switch (e) {
    case E::kNoNetworkPower:
      return "No network power";
  }
}

namespace fc::blockchain::weight {
  using primitives::BigInt;
  using primitives::StoragePower;
  using vm::actor::kStoragePowerAddress;
  using vm::actor::builtin::states::StateProvider;
  using vm::state::StateTreeImpl;

  constexpr uint64_t kWRatioNum{1};
  constexpr uint64_t kWRatioDen{2};
  constexpr uint64_t kBlocksPerEpoch{5};

  WeightCalculatorImpl::WeightCalculatorImpl(std::shared_ptr<Ipld> ipld)
      : ipld_{std::move(ipld)} {}

  outcome::result<BigInt> WeightCalculatorImpl::calculateWeight(
      const Tipset &tipset) {
    StateProvider provider(ipld_);
    OUTCOME_TRY(actor,
                StateTreeImpl{ipld_, tipset.getParentStateRoot()}.get(
                    kStoragePowerAddress));
    OUTCOME_TRY(state, provider.getPowerActorState(actor));
    const StoragePower network_power = state->total_qa_power;

    if (network_power <= 0) {
      return outcome::failure(WeightCalculatorError::kNoNetworkPower);
    }
    BigInt log{boost::multiprecision::msb(network_power) << 8};
    auto j{0};
    for (auto &block : tipset.blks) {
      j += block.election_proof.win_count;
    }
    return tipset.getParentWeight() + log
           + bigdiv(log * j * kWRatioNum, kBlocksPerEpoch * kWRatioDen);
  }

}  // namespace fc::blockchain::weight
