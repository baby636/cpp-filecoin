# Copyright Soramitsu Co., Ltd. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

add_library(tipset
    chain.cpp
    load.cpp
    tipset.cpp
    tipset_key.cpp
    )
target_link_libraries(tipset
    block
    buffer
    cid
    const
    logger
    )
