#
# Copyright Soramitsu Co., Ltd. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0
#

add_library(verified_registry_actor_v0
    verified_registry_actor.cpp
    verified_registry_actor_state.cpp
    verified_registry_actor_utils.cpp
    )
target_link_libraries(verified_registry_actor_v0
    actor
    outcome
    )
