/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "vm/actor/builtin/states/state.hpp"

#include "adt/map.hpp"
#include "adt/uvarint_key.hpp"
#include "common/buffer.hpp"
#include "common/outcome.hpp"
#include "primitives/address/address.hpp"
#include "primitives/chain_epoch/chain_epoch.hpp"
#include "primitives/types.hpp"
#include "vm/actor/builtin/types/multisig/transaction.hpp"

// Forward declaration
namespace fc::vm::runtime {
  class Runtime;
}

namespace fc::vm::actor::builtin::states {
  using primitives::ChainEpoch;
  using primitives::EpochDuration;
  using primitives::TokenAmount;
  using primitives::address::Address;
  using types::multisig::Transaction;
  using types::multisig::TransactionId;
  using types::multisig::TransactionKeyer;

  /**
   * State of Multisig Actor instance
   */
  struct MultisigActorState : State {
    std::vector<Address> signers;
    size_t threshold{0};
    TransactionId next_transaction_id{0};

    // Linear lock
    TokenAmount initial_balance{0};
    ChainEpoch start_epoch{0};
    EpochDuration unlock_duration{0};

    // List of pending transactions
    adt::Map<Transaction, TransactionKeyer> pending_transactions;

    // Methods
    virtual std::shared_ptr<MultisigActorState> copy() const = 0;

    void setLocked(const ChainEpoch &start_epoch,
                   const EpochDuration &unlock_duration,
                   const TokenAmount &lockedAmount);

    /**
     * Check if address is signer
     * @param address - address to check
     * @return true if address is signer
     */
    inline bool isSigner(const Address &address) const {
      return std::find(signers.begin(), signers.end(), address)
             != signers.end();
    }

    /**
     * Get pending transaction
     * @param tx_id - transaction id
     * @return transaction
     */
    outcome::result<Transaction> getPendingTransaction(
        const TransactionId &tx_id) const;

    /**
     * Get transaction from the state tree
     * @param tx_id - transaction id
     * @param proposal_hash - expected hash of transaction
     * @return transaction
     */
    outcome::result<Transaction> getTransaction(
        fc::vm::runtime::Runtime &runtime,
        const TransactionId &tx_id,
        const Buffer &proposal_hash) const;
  };

  using MultisigActorStatePtr = std::shared_ptr<MultisigActorState>;

}  // namespace fc::vm::actor::builtin::states
