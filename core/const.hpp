/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "primitives/chain_epoch/chain_epoch.hpp"
#include "primitives/types.hpp"

namespace fc {
  using primitives::ChainEpoch;
  using primitives::EpochDuration;
  using primitives::StoragePower;

  constexpr auto kSecondsInHour{60 * 60};
  constexpr auto kSecondsInDay{24 * kSecondsInHour};

  extern size_t kEpochDurationSeconds;
  extern size_t kEpochsInHour;
  extern size_t kEpochsInDay;
  extern size_t kEpochsInYear;

  extern size_t kPropagationDelaySecs;

  constexpr auto kBaseFeeMaxChangeDenom{8};
  constexpr auto kBlockGasLimit{10000000000};
  constexpr auto kBlockGasTarget{kBlockGasLimit / 2};
  constexpr auto kBlockMessageLimit{10000};
  constexpr auto kBlocksPerEpoch{5};
  constexpr auto kConsensusMinerMinMiners{3};
  constexpr uint64_t kFilReserve{300000000};
  constexpr uint64_t kFilecoinPrecision{1000000000000000000};
  constexpr auto kGasLimitOverestimation{1.25};
  constexpr auto kMessageConfidence{5};
  constexpr auto kMinimumBaseFee{100};
  constexpr auto kPackingEfficiencyDenom{5};
  constexpr auto kPackingEfficiencyNum{4};

  // ******************
  // Network versions
  // ******************
  /**
   * Network version heights. The last height before network upgrade.
   */
  extern ChainEpoch kUpgradeBreezeHeight;
  extern ChainEpoch kUpgradeSmokeHeight;
  extern ChainEpoch kUpgradeIgnitionHeight;
  extern ChainEpoch kUpgradeRefuelHeight;
  extern ChainEpoch kUpgradeActorsV2Height;
  extern ChainEpoch kUpgradeTapeHeight;
  extern ChainEpoch kUpgradeLiftoffHeight;
  extern ChainEpoch kUpgradeKumquatHeight;
  extern ChainEpoch kUpgradeCalicoHeight;
  extern ChainEpoch kUpgradePersianHeight;
  extern ChainEpoch kUpgradeOrangeHeight;
  extern ChainEpoch kUpgradeClausHeight;
  extern ChainEpoch kUpgradeActorsV3Height;
  extern ChainEpoch kUpgradeNorwegianHeight;
  extern ChainEpoch kUpgradeActorsV4Height;

  extern EpochDuration kBreezeGasTampingDuration;

  constexpr uint64_t kDefaultStorageWeight{10};

  extern EpochDuration kInteractivePoRepConfidence;

  /**
   * Sets parameters for test network with 2k seal proof type
   * Identical to Lotus 2k build
   */
  void setParams2K();

  /**
   * Sets parameters for test network with high upgrade heights
   * May be useful when want to avoid network upgrades
   */
  void setParamsNoUpgrades();
}  // namespace fc
