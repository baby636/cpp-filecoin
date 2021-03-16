/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "codec/cbor/streams_annotation.hpp"
#include "vm/actor/builtin/states/init_actor_state.hpp"

namespace fc::vm::actor::builtin::v3::init {
  struct InitActorState : states::InitActorState {
    outcome::result<Buffer> toCbor() const override;
  };
  CBOR_TUPLE(InitActorState, address_map, next_id, network_name)
}  // namespace fc::vm::actor::builtin::v3::init

namespace fc {
  template <>
  struct Ipld::Visit<vm::actor::builtin::v3::init::InitActorState> {
    template <typename Visitor>
    static void call(vm::actor::builtin::v3::init::InitActorState &state,
                     const Visitor &visit) {
      visit(state.address_map);
    }
  };
}  // namespace fc