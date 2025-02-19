/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "miner/storage_fsm/impl/sealing_impl.hpp"

#include <gtest/gtest.h>
#include <chrono>

#include "primitives/sector/sector.hpp"
#include "storage/in_memory/in_memory_storage.hpp"
#include "storage/ipfs/impl/in_memory_datastore.hpp"
#include "testutil/context_wait.hpp"
#include "testutil/literals.hpp"
#include "testutil/mocks/libp2p/scheduler_mock.hpp"
#include "testutil/mocks/miner/events_mock.hpp"
#include "testutil/mocks/miner/precommit_policy_mock.hpp"
#include "testutil/mocks/primitives/stored_counter_mock.hpp"
#include "testutil/mocks/proofs/proof_engine_mock.hpp"
#include "testutil/mocks/sector_storage/manager_mock.hpp"
#include "testutil/outcome.hpp"
#include "vm/actor/builtin/v0/codes.hpp"
#include "vm/actor/builtin/v0/miner/miner_actor_state.hpp"

namespace fc::mining {

  using adt::Channel;
  using api::DealId;
  using api::DomainSeparationTag;
  using api::InvocResult;
  using api::MinerInfo;
  using api::MsgWait;
  using api::StorageDeal;
  using api::TipsetCPtr;
  using api::UnsignedMessage;
  using api::Wait;
  using crypto::randomness::Randomness;
  using fc::storage::ipfs::InMemoryDatastore;
  using libp2p::protocol::SchedulerMock;
  using markets::storage::DealProposal;
  using primitives::CounterMock;
  using primitives::block::BlockHeader;
  using primitives::sector::Proof;
  using sector_storage::Commit1Output;
  using sector_storage::ManagerMock;
  using storage::InMemoryStorage;
  using testing::_;
  using types::kDealSectorPriority;
  using types::Piece;
  using types::SectorInfo;
  using vm::actor::Actor;
  using vm::actor::builtin::types::miner::kPreCommitChallengeDelay;
  using vm::actor::builtin::types::miner::SectorOnChainInfo;
  using vm::actor::builtin::v0::miner::MinerActorState;
  using vm::message::SignedMessage;
  using vm::runtime::MessageReceipt;

  class SealingTest : public testing::Test {
   protected:
    void SetUp() override {
      seal_proof_type_ = RegisteredSealProof::kStackedDrg2KiBV1;
      auto sector_size = primitives::sector::getSectorSize(seal_proof_type_);
      ASSERT_FALSE(sector_size.has_error());
      sector_size_ = PaddedPieceSize(sector_size.value());

      api_ = std::make_shared<FullNodeApi>();
      events_ = std::make_shared<EventsMock>();
      miner_id_ = 42;
      miner_addr_ = Address::makeFromId(miner_id_);
      counter_ = std::make_shared<CounterMock>();
      kv_ = std::make_shared<InMemoryStorage>();

      update_sector_id_ = 2;
      SectorInfo info;
      info.sector_number = update_sector_id_;
      info.state = SealingState::kProving;
      info.pieces = {Piece{
          .piece =
              PieceInfo{
                  .size = PaddedPieceSize(sector_size_),
                  .cid = "010001020011"_cid,
              },
          .deal_info = boost::none,
      }};
      EXPECT_OUTCOME_TRUE(buf, codec::cbor::encode(info));
      Buffer key;
      key.put("empty_sector");
      EXPECT_OUTCOME_TRUE_1(kv_->put(key, buf));

      proofs_ = std::make_shared<proofs::ProofEngineMock>();

      manager_ = std::make_shared<ManagerMock>();

      EXPECT_CALL(*manager_, getProofEngine())
          .WillRepeatedly(testing::Return(proofs_));

      EXPECT_CALL(*manager_, getSectorSize())
          .WillRepeatedly(testing::Return(sector_size_));

      policy_ = std::make_shared<PreCommitPolicyMock>();
      context_ = std::make_shared<boost::asio::io_context>();

      config_ = Config{
          .max_wait_deals_sectors = 2,
          .max_sealing_sectors = 0,
          .max_sealing_sectors_for_deals = 0,
          .wait_deals_delay = std::chrono::hours(6).count(),
      };

      scheduler_ = std::make_shared<SchedulerMock>();

      EXPECT_OUTCOME_TRUE(sealing,
                          SealingImpl::newSealing(api_,
                                                  events_,
                                                  miner_addr_,
                                                  counter_,
                                                  kv_,
                                                  manager_,
                                                  policy_,
                                                  context_,
                                                  scheduler_,
                                                  config_));
      sealing_ = sealing;
    }

    void TearDown() override {
      context_->stop();
    }

    uint64_t update_sector_id_;
    RegisteredSealProof seal_proof_type_;

    PaddedPieceSize sector_size_;
    Config config_;
    std::shared_ptr<FullNodeApi> api_;
    std::shared_ptr<EventsMock> events_;
    uint64_t miner_id_;
    Address miner_addr_;
    std::shared_ptr<CounterMock> counter_;
    std::shared_ptr<InMemoryStorage> kv_;
    std::shared_ptr<ManagerMock> manager_;
    std::shared_ptr<proofs::ProofEngineMock> proofs_;
    std::shared_ptr<PreCommitPolicyMock> policy_;
    std::shared_ptr<boost::asio::io_context> context_;
    std::shared_ptr<SchedulerMock> scheduler_;

    std::shared_ptr<Sealing> sealing_;
  };

  /**
   * @given address
   * @when try to get address
   * @then address was getted
   */
  TEST_F(SealingTest, getAddress) {
    ASSERT_EQ(miner_addr_, sealing_->getAddress());
  }

  /**
   * @given nothing
   * @when try to get not exist sector
   * @then SealingError::kCannotFindSector occurs
   */
  TEST_F(SealingTest, getSectorInfoNotFound) {
    EXPECT_OUTCOME_ERROR(SealingError::kCannotFindSector,
                         sealing_->getSectorInfo(1));
  }

  /**
   * @given nothing
   * @when try to remove not exist sector
   * @then SealingError::kCannotFindSector occurs
   */
  TEST_F(SealingTest, RemoveNotFound) {
    EXPECT_OUTCOME_ERROR(SealingError::kCannotFindSector, sealing_->remove(1));
  }

  /**
   * @given sector(in Proving state)
   * @when try to remove
   * @then sector was removed
   */
  TEST_F(SealingTest, Remove) {
    UnpaddedPieceSize piece_size(127);
    PieceData piece("/dev/random");
    DealInfo deal{
        .publish_cid = "010001020001"_cid,
        .deal_id = 0,
        .deal_schedule =
            {
                .start_epoch = 0,
                .end_epoch = 1,
            },
        .is_keep_unsealed = true,
    };

    SectorNumber sector = 1;
    EXPECT_CALL(*counter_, next()).WillOnce(testing::Return(sector));

    api_->StateMinerInfo =
        [&](const Address &address,
            const TipsetKey &tipset_key) -> outcome::result<MinerInfo> {
      if (address == miner_addr_) {
        MinerInfo info;
        info.seal_proof_type = seal_proof_type_;
        return info;
      }
      return ERROR_TEXT("ERROR");
    };

    PieceInfo info{
        .size = piece_size.padded(),
        .cid = "010001020001"_cid,
    };

    EXPECT_CALL(*manager_,
                addPiece(SectorId{.miner = miner_id_, .sector = sector},
                         gsl::span<const UnpaddedPieceSize>(),
                         piece_size,
                         _,
                         kDealSectorPriority))
        .WillOnce(testing::Return(info));

    EXPECT_OUTCOME_TRUE_1(
        sealing_->addPieceToAnySector(piece_size, piece, deal));

    EXPECT_OUTCOME_TRUE(info_before, sealing_->getSectorInfo(sector));
    EXPECT_EQ(info_before->state, SealingState::kStateUnknown);
    EXPECT_OUTCOME_TRUE_1(
        sealing_->forceSectorState(sector, SealingState::kProving));
    EXPECT_OUTCOME_TRUE_1(sealing_->remove(sector));

    EXPECT_CALL(*manager_,
                remove(SectorId{.miner = miner_id_, .sector = sector}))
        .WillOnce(testing::Return(outcome::success()));

    runForSteps(*context_, 100);

    EXPECT_OUTCOME_TRUE(sector_info, sealing_->getSectorInfo(sector));
    EXPECT_EQ(sector_info->state, SealingState::kRemoved);
  }

  /**
   * @given piece, deal(not published)
   * @when try to add piece to Sector
   * @then SealingError::kNotPublishedDeal occurs
   */
  TEST_F(SealingTest, addPieceToAnySectorNotPublishedDeal) {
    UnpaddedPieceSize piece_size(127);
    PieceData piece("/dev/random");
    DealInfo deal{
        .publish_cid = boost::none,
        .deal_id = 0,
        .deal_schedule =
            {
                .start_epoch = 0,
                .end_epoch = 1,
            },
        .is_keep_unsealed = true,
    };
    EXPECT_OUTCOME_ERROR(
        SealingError::kNotPublishedDeal,
        sealing_->addPieceToAnySector(piece_size, piece, deal));
  }

  /**
   * @given piece(size not valid)
   * @when try to add piece to Sector
   * @then SealingError::kCannotAllocatePiece occurs
   */
  TEST_F(SealingTest, addPieceToAnySectorCannotAllocatePiece) {
    UnpaddedPieceSize piece_size(128);
    PieceData piece("/dev/random");
    DealInfo deal{
        .publish_cid = "010001020001"_cid,
        .deal_id = 0,
        .deal_schedule =
            {
                .start_epoch = 0,
                .end_epoch = 1,
            },
        .is_keep_unsealed = true,
    };
    EXPECT_OUTCOME_ERROR(
        SealingError::kCannotAllocatePiece,
        sealing_->addPieceToAnySector(piece_size, piece, deal));
  }

  /**
   * @given large piece(size > sector size)
   * @when try to add piece to Sector
   * @then SealingError::kPieceNotFit occurs
   */
  TEST_F(SealingTest, addPieceToAnySectorPieceNotFit) {
    UnpaddedPieceSize piece_size(4064);
    PieceData piece("/dev/random");
    DealInfo deal{
        .publish_cid = "010001020001"_cid,
        .deal_id = 0,
        .deal_schedule =
            {
                .start_epoch = 0,
                .end_epoch = 1,
            },
        .is_keep_unsealed = true,
    };

    EXPECT_OUTCOME_ERROR(
        SealingError::kPieceNotFit,
        sealing_->addPieceToAnySector(piece_size, piece, deal));
  }

  /**
   * @given piece
   * @when try to add piece to Sector
   * @then success and state is WaitDeals
   */
  TEST_F(SealingTest, addPieceToAnySectorWithoutStartPacking) {
    UnpaddedPieceSize piece_size(127);
    PieceData piece("/dev/random");
    DealInfo deal{
        .publish_cid = "010001020001"_cid,
        .deal_id = 0,
        .deal_schedule =
            {
                .start_epoch = 0,
                .end_epoch = 1,
            },
        .is_keep_unsealed = true,
    };

    SectorNumber sector = 1;
    EXPECT_CALL(*counter_, next()).WillOnce(testing::Return(sector));

    api_->StateMinerInfo =
        [&](const Address &address,
            const TipsetKey &tipset_key) -> outcome::result<MinerInfo> {
      if (address == miner_addr_) {
        MinerInfo info;
        info.seal_proof_type = seal_proof_type_;
        return info;
      }
      return ERROR_TEXT("ERROR");
    };

    PieceInfo info{
        .size = piece_size.padded(),
        .cid = "010001020001"_cid,
    };

    EXPECT_CALL(*manager_,
                addPiece(SectorId{.miner = miner_id_, .sector = sector},
                         gsl::span<const UnpaddedPieceSize>(),
                         piece_size,
                         _,
                         kDealSectorPriority))
        .WillOnce(testing::Return(info));

    EXPECT_OUTCOME_TRUE(piece_attribute,
                        sealing_->addPieceToAnySector(piece_size, piece, deal));
    EXPECT_EQ(piece_attribute.sector, sector);
    EXPECT_EQ(piece_attribute.offset, 0);
    EXPECT_EQ(piece_attribute.size, piece_size);

    runForSteps(*context_, 100);

    EXPECT_OUTCOME_TRUE(sector_info, sealing_->getSectorInfo(sector));
    EXPECT_EQ(sector_info->sector_number, sector);
    EXPECT_EQ(sector_info->state, SealingState::kWaitDeals);
  }

  /**
   * @given sector(not in Proving state)
   * @when try to mark for upgrade
   * @then SealingError::kNotProvingState occurs
   */
  TEST_F(SealingTest, MarkForUpgradeNotProvingState) {
    UnpaddedPieceSize piece_size(127);
    PieceData piece("/dev/random");
    DealInfo deal{
        .publish_cid = "010001020001"_cid,
        .deal_id = 0,
        .deal_schedule =
            {
                .start_epoch = 0,
                .end_epoch = 1,
            },
        .is_keep_unsealed = true,
    };

    SectorNumber sector = 1;
    EXPECT_CALL(*counter_, next()).WillOnce(testing::Return(sector));

    api_->StateMinerInfo =
        [&](const Address &address,
            const TipsetKey &tipset_key) -> outcome::result<MinerInfo> {
      if (address == miner_addr_) {
        MinerInfo info;
        info.seal_proof_type = seal_proof_type_;
        return info;
      }
      return ERROR_TEXT("ERROR");
    };

    PieceInfo info{
        .size = piece_size.padded(),
        .cid = "010001020001"_cid,
    };

    EXPECT_CALL(*manager_,
                addPiece(SectorId{.miner = miner_id_, .sector = sector},
                         gsl::span<const UnpaddedPieceSize>(),
                         piece_size,
                         _,
                         kDealSectorPriority))
        .WillOnce(testing::Return(info));

    EXPECT_OUTCOME_TRUE_1(
        sealing_->addPieceToAnySector(piece_size, piece, deal));

    EXPECT_OUTCOME_ERROR(SealingError::kNotProvingState,
                         sealing_->markForUpgrade(sector));
  }

  /**
   * @given sector(with several pieces)
   * @when try to mark for upgrade
   * @then SealingError::kUpgradeSeveralPieces occurs
   */
  TEST_F(SealingTest, MarkForUpgradeSeveralPieces) {
    UnpaddedPieceSize piece_size(127);
    PieceData piece("/dev/random");
    DealInfo deal{
        .publish_cid = "010001020001"_cid,
        .deal_id = 0,
        .deal_schedule =
            {
                .start_epoch = 0,
                .end_epoch = 1,
            },
        .is_keep_unsealed = true,
    };

    SectorNumber sector = 1;
    EXPECT_CALL(*counter_, next()).WillOnce(testing::Return(sector));

    api_->StateMinerInfo =
        [&](const Address &address,
            const TipsetKey &tipset_key) -> outcome::result<MinerInfo> {
      if (address == miner_addr_) {
        MinerInfo info;
        info.seal_proof_type = seal_proof_type_;
        return info;
      }
      return ERROR_TEXT("ERROR");
    };

    PieceInfo info1{
        .size = piece_size.padded(),
        .cid = "010001020001"_cid,
    };
    PieceInfo info2{
        .size = piece_size.padded(),
        .cid = "010001020002"_cid,
    };

    EXPECT_CALL(*manager_,
                addPiece(SectorId{.miner = miner_id_, .sector = sector},
                         gsl::span<const UnpaddedPieceSize>(),
                         piece_size,
                         _,
                         kDealSectorPriority))
        .WillOnce(testing::Return(info1));

    std::vector<UnpaddedPieceSize> exist_pieces({piece_size});
    EXPECT_CALL(*manager_,
                addPiece(SectorId{.miner = miner_id_, .sector = sector},
                         gsl::span<const UnpaddedPieceSize>(exist_pieces),
                         piece_size,
                         _,
                         kDealSectorPriority))
        .WillOnce(testing::Return(info2));

    EXPECT_OUTCOME_TRUE_1(
        sealing_->addPieceToAnySector(piece_size, piece, deal));
    EXPECT_OUTCOME_TRUE_1(
        sealing_->addPieceToAnySector(piece_size, piece, deal));

    EXPECT_OUTCOME_TRUE_1(
        sealing_->forceSectorState(sector, SealingState::kProving));

    runForSteps(*context_, 100);

    EXPECT_OUTCOME_ERROR(SealingError::kUpgradeSeveralPieces,
                         sealing_->markForUpgrade(sector));
  }

  /**
   * @given sector(has deal)
   * @when try to mark for upgrade
   * @then SealingError::kUpgradeWithDeal occurs
   */
  TEST_F(SealingTest, MarkForUpgradeWithDeal) {
    UnpaddedPieceSize piece_size(127);
    PieceData piece("/dev/random");
    DealInfo deal{
        .publish_cid = "010001020001"_cid,
        .deal_id = 0,
        .deal_schedule =
            {
                .start_epoch = 0,
                .end_epoch = 1,
            },
        .is_keep_unsealed = true,
    };

    SectorNumber sector = 1;
    EXPECT_CALL(*counter_, next()).WillOnce(testing::Return(sector));

    api_->StateMinerInfo =
        [&](const Address &address,
            const TipsetKey &tipset_key) -> outcome::result<MinerInfo> {
      if (address == miner_addr_) {
        MinerInfo info;
        info.seal_proof_type = seal_proof_type_;
        return info;
      }
      return ERROR_TEXT("ERROR");
    };

    PieceInfo info{
        .size = piece_size.padded(),
        .cid = "010001020001"_cid,
    };

    EXPECT_CALL(*manager_,
                addPiece(SectorId{.miner = miner_id_, .sector = sector},
                         gsl::span<const UnpaddedPieceSize>(),
                         piece_size,
                         _,
                         kDealSectorPriority))
        .WillOnce(testing::Return(info));

    EXPECT_OUTCOME_TRUE_1(
        sealing_->addPieceToAnySector(piece_size, piece, deal));

    EXPECT_OUTCOME_TRUE_1(
        sealing_->forceSectorState(sector, SealingState::kProving));

    runForSteps(*context_, 100);

    EXPECT_OUTCOME_ERROR(SealingError::kUpgradeWithDeal,
                         sealing_->markForUpgrade(sector));
  }

  /**
   * @given sector with blank piece
   * @when try to mark for upgrade
   * @then success
   */
  TEST_F(SealingTest, MarkForUpgrade) {
    EXPECT_EQ(sealing_->isMarkedForUpgrade(update_sector_id_), false);
    EXPECT_OUTCOME_TRUE_1(sealing_->markForUpgrade(update_sector_id_));
    EXPECT_EQ(sealing_->isMarkedForUpgrade(update_sector_id_), true);
  }
  /**
   * @given merked sector
   * @when try to mark for upgrade
   * @then SealingError::kAlreadyUpgradeMarked occurs
   */
  TEST_F(SealingTest, MarkForUpgradeAlreadyMarked) {
    EXPECT_OUTCOME_TRUE_1(sealing_->markForUpgrade(update_sector_id_));
    EXPECT_EQ(sealing_->isMarkedForUpgrade(update_sector_id_), true);
    EXPECT_OUTCOME_ERROR(SealingError::kAlreadyUpgradeMarked,
                         sealing_->markForUpgrade(update_sector_id_));
  }

  /**
   * @given sector in sealing
   * @when try to get List of sectors
   * @then list size is 2
   */
  TEST_F(SealingTest, ListOfSectors) {
    UnpaddedPieceSize piece_size(127);
    PieceData piece("/dev/random");
    DealInfo deal{
        .publish_cid = "010001020001"_cid,
        .deal_id = 0,
        .deal_schedule =
            {
                .start_epoch = 0,
                .end_epoch = 1,
            },
        .is_keep_unsealed = true,
    };

    SectorNumber sector = 1;
    EXPECT_CALL(*counter_, next()).WillOnce(testing::Return(sector));

    api_->StateMinerInfo =
        [&](const Address &address,
            const TipsetKey &tipset_key) -> outcome::result<MinerInfo> {
      if (address == miner_addr_) {
        MinerInfo info;
        info.seal_proof_type = seal_proof_type_;
        return info;
      }
      return ERROR_TEXT("ERROR");
    };

    PieceInfo info{
        .size = piece_size.padded(),
        .cid = "010001020001"_cid,
    };

    EXPECT_CALL(*manager_,
                addPiece(SectorId{.miner = miner_id_, .sector = sector},
                         gsl::span<const UnpaddedPieceSize>(),
                         piece_size,
                         _,
                         kDealSectorPriority))
        .WillOnce(testing::Return(info));

    EXPECT_OUTCOME_TRUE_1(
        sealing_->addPieceToAnySector(piece_size, piece, deal));

    auto sectors = sealing_->getListSectors();
    ASSERT_EQ(sectors.size(), 2);
  }

  /**
   * @given sector
   * @when try to seal sector to Proving
   * @then success
   */
  TEST_F(SealingTest, processToProving) {
    UnpaddedPieceSize piece_size(2032);
    PieceData piece("/dev/random");
    DealInfo deal{
        .publish_cid = "010001020001"_cid,
        .deal_id = 0,
        .deal_schedule =
            {
                .start_epoch = 1,
                .end_epoch = 2,
            },
        .is_keep_unsealed = true,
    };

    SectorNumber sector = 1;
    EXPECT_CALL(*counter_, next()).WillOnce(testing::Return(sector));

    SectorId sector_id{.miner = miner_id_, .sector = sector};

    api_->StateMinerInfo =
        [&](const Address &address,
            const TipsetKey &tipset_key) -> outcome::result<MinerInfo> {
      if (address == miner_addr_) {
        MinerInfo info;
        info.seal_proof_type = seal_proof_type_;
        return info;
      }
      return ERROR_TEXT("ERROR");
    };

    PieceInfo info{
        .size = piece_size.padded(),
        .cid = "010001020001"_cid,
    };

    EXPECT_CALL(*manager_,
                addPiece(sector_id,
                         gsl::span<const UnpaddedPieceSize>(),
                         piece_size,
                         _,
                         kDealSectorPriority))
        .WillOnce(testing::Return(info));

    EXPECT_OUTCOME_TRUE_1(
        sealing_->addPieceToAnySector(piece_size, piece, deal));

    // Precommit 1
    TipsetKey key({"010001020002"_cid});
    auto tipset = std::make_shared<Tipset>(key, std::vector<BlockHeader>());

    api_->ChainHead = [&]() -> outcome::result<TipsetCPtr> { return tipset; };

    DealProposal proposal;
    proposal.piece_cid = info.cid;
    proposal.piece_size = info.size;
    proposal.start_epoch = tipset->height() + 1;
    proposal.provider = miner_addr_;
    StorageDeal storage_deal;
    storage_deal.proposal = proposal;

    api_->StateMarketStorageDeal =
        [&](DealId deal_id,
            const TipsetKey &tipset_key) -> outcome::result<StorageDeal> {
      if (deal_id == deal.deal_id and tipset_key == key) {
        return storage_deal;
      }
      return ERROR_TEXT("ERROR");
    };

    auto actor_key{"010001020003"_cid};
    auto ipld{std::make_shared<InMemoryDatastore>()};
    MinerActorState actor_state;
    actor_state.miner_info = "010001020004"_cid;
    actor_state.vesting_funds = "010001020004"_cid;
    actor_state.allocated_sectors = "010001020004"_cid;
    actor_state.deadlines = "010001020006"_cid;
    actor_state.precommitted_sectors =
        adt::Map<SectorPreCommitOnChainInfo, adt::UvarintKeyer>(ipld);
    SectorPreCommitOnChainInfo some_info;
    some_info.info.sealed_cid = "010001020006"_cid;
    EXPECT_OUTCOME_TRUE_1(
        actor_state.precommitted_sectors.set(sector + 1, some_info));
    EXPECT_OUTCOME_TRUE(cid_root,
                        actor_state.precommitted_sectors.hamt.flush());
    SectorPreCommitOnChainInfo precommit_info;
    precommit_info.info.seal_epoch = 3;
    precommit_info.info.sealed_cid = "010001020007"_cid;
    actor_state.sectors =
        adt::Array<SectorOnChainInfo>("010001020008"_cid, ipld);
    actor_state.precommitted_setctors_expiry =
        adt::Array<api::RleBitset>("010001020009"_cid, ipld);
    api_->ChainReadObj = [&](CID key) -> outcome::result<Buffer> {
      if (key == actor_key) {
        return codec::cbor::encode(actor_state);
      }
      if (key == cid_root) {
        OUTCOME_TRY(root, ipld->getCbor<storage::hamt::Node>(cid_root));
        return codec::cbor::encode(root);
      }
      if (key == actor_state.allocated_sectors) {
        return codec::cbor::encode(primitives::RleBitset());
      }
      return ERROR_TEXT("ERROR");
    };

    Actor actor;
    actor.code = vm::actor::builtin::v0::kStorageMinerCodeId;
    actor.head = actor_key;
    api_->StateGetActor = [&](const Address &addr, const TipsetKey &tipset_key)
        -> outcome::result<Actor> { return actor; };

    Randomness rand{{1, 2, 3}};

    api_->ChainGetRandomnessFromTickets =
        [&](const TipsetKey &,
            DomainSeparationTag,
            ChainEpoch,
            const Buffer &) -> outcome::result<Randomness> { return rand; };

    std::vector<PieceInfo> infos = {info};

    types::PreCommit1Output pc1o({4, 5, 6});

    EXPECT_CALL(*manager_,
                sealPreCommit1(sector_id,
                               rand,
                               gsl::span<const PieceInfo>(infos),
                               kDealSectorPriority))
        .WillOnce(testing::Return(outcome::success(pc1o)));

    // Precommit 2

    sector_storage::SectorCids cids{.sealed_cid = "010001020010"_cid,
                                    .unsealed_cid = "010001020011"_cid};

    EXPECT_CALL(*manager_, sealPreCommit2(sector_id, pc1o, kDealSectorPriority))
        .WillOnce(testing::Return(outcome::success(cids)));

    // Precommitting

    api_->StateCall =
        [&](const UnsignedMessage &message,
            const TipsetKey &tipset_key) -> outcome::result<InvocResult> {
      InvocResult result;
      OUTCOME_TRY(unsealed_buffer, codec::cbor::encode(cids.unsealed_cid));
      result.receipt = MessageReceipt{
          .exit_code = vm::VMExitCode::kOk,
          .return_value = unsealed_buffer,
      };
      return result;
    };

    api_->StateNetworkVersion = [](const TipsetKey &tipset_key)
        -> outcome::result<api::NetworkVersion> {
      return api::NetworkVersion::kVersion7;
    };

    EXPECT_CALL(*policy_, expiration(_)).WillOnce(testing::Return(0));

    api_->StateMinerPreCommitDepositForPower =
        [](const Address &,
           const SectorPreCommitInfo &,
           const TipsetKey &) -> outcome::result<TokenAmount> { return 10; };

    CID precommit_msg_cid;
    CID commit_msg_cid;  // for commit stage
    api_->MpoolPushMessage = [&precommit_msg_cid, &commit_msg_cid](
                                 const UnsignedMessage &msg,
                                 const boost::optional<api::MessageSendSpec> &)
        -> outcome::result<SignedMessage> {
      if (precommit_msg_cid == CID()) {
        precommit_msg_cid = msg.getCid();
        return SignedMessage{.message = msg, .signature = BlsSignature()};
      }

      commit_msg_cid = msg.getCid();
      return SignedMessage{.message = msg, .signature = BlsSignature()};
    };

    // Precommitted

    std::vector<CID> precommit_tipset_cids(
        {"010001020011"_cid, "010001020012"_cid});
    TipsetKey precommit_tipset_key(precommit_tipset_cids);
    std::vector<CID> commit_tipset_cids(
        {"010001020013"_cid, "010001020014"_cid});
    TipsetKey commit_tipset_key(commit_tipset_cids);
    EpochDuration height = 3;
    api_->StateWaitMsg = [&](const CID &msg_cid,
                             uint64_t conf) -> outcome::result<Wait<MsgWait>> {
      if (msg_cid == precommit_msg_cid) {
        // make precommit for actor
        {
          SectorPreCommitOnChainInfo new_info;
          new_info.precommit_epoch = height;
          new_info.info.sealed_cid = cids.sealed_cid;
          OUTCOME_TRY(actor_state.precommitted_sectors.set(sector, new_info));
          OUTCOME_TRYA(cid_root, actor_state.precommitted_sectors.hamt.flush());
        }

        auto chan{std::make_shared<Channel<outcome::result<MsgWait>>>()};
        MsgWait result;
        result.tipset = precommit_tipset_key;
        result.receipt.exit_code = vm::VMExitCode::kOk;
        chan->write(result);
        return Wait(chan);
      }
      if (msg_cid == commit_msg_cid) {
        auto chan{std::make_shared<Channel<outcome::result<MsgWait>>>()};
        MsgWait result;
        result.tipset = commit_tipset_key;
        result.receipt.exit_code = vm::VMExitCode::kOk;
        chan->write(result);
        return Wait(chan);
      }

      return ERROR_TEXT("ERROR");
    };

    // Wait Seed

    Randomness seed{{6, 7, 8, 9}};
    api_->ChainGetRandomnessFromBeacon =
        [&](const TipsetKey &,
            DomainSeparationTag,
            ChainEpoch,
            const Buffer &) -> outcome::result<Randomness> { return seed; };

    EXPECT_CALL(*events_,
                chainAt(_,
                        _,
                        kInteractivePoRepConfidence,
                        height + kPreCommitChallengeDelay))
        .WillOnce(testing::Invoke(
            [](auto &apply, auto, auto, auto) -> outcome::result<void> {
              OUTCOME_TRY(apply({}, 0));
              return outcome::success();
            }));

    // Commiting

    Commit1Output c1o({1, 2, 3, 4, 5, 6});
    EXPECT_CALL(*manager_,
                sealCommit1(sector_id,
                            rand,
                            seed,
                            gsl::span<const PieceInfo>(infos),
                            cids,
                            kDealSectorPriority))
        .WillOnce(testing::Return(c1o));
    Proof proof({7, 6, 5, 4, 3, 2, 1});
    EXPECT_CALL(*manager_, sealCommit2(sector_id, c1o, kDealSectorPriority))
        .WillOnce(testing::Return(proof));

    EXPECT_CALL(*proofs_, verifySeal(_))
        .WillOnce(testing::Return(outcome::success(true)));

    api_->StateMinerInitialPledgeCollateral =
        [](const Address &,
           const SectorPreCommitInfo &,
           const TipsetKey &) -> outcome::result<TokenAmount> { return 0; };

    // Commit Wait

    api_->StateSectorGetInfo =
        [&](const Address &, SectorNumber, const TipsetKey &key)
        -> outcome::result<boost::optional<SectorOnChainInfo>> {
      if (key == commit_tipset_key) {
        return SectorOnChainInfo{};
      }

      return ERROR_TEXT("ERROR");
    };

    // Finalize
    EXPECT_CALL(*manager_, finalizeSector(sector_id, _, kDealSectorPriority))
        .WillOnce(testing::Return(outcome::success()));

    auto state{SealingState::kStateUnknown};
    while (state != SealingState::kProving) {
      runForSteps(*context_, 100);
      EXPECT_OUTCOME_TRUE(sector_info, sealing_->getSectorInfo(sector));
      ASSERT_NE(sector_info->state, state);
      state = sector_info->state;
    }
  }

  /**
   * @given sealing, 1 sector
   * @when try to add pledge sector
   * @then 2 sectors in sealing
   */
  TEST_F(SealingTest, pledgeSector) {
    SectorNumber sector = 1;
    EXPECT_CALL(*counter_, next()).WillOnce(testing::Return(sector));

    SectorId sector_id{
        .miner = 42,
        .sector = sector,
    };

    PieceInfo info{
        .size = PaddedPieceSize(sector_size_),
        .cid = "010001020002"_cid,
    };

    std::vector<UnpaddedPieceSize> exist_pieces = {};
    EXPECT_CALL(*manager_,
                addPiece(sector_id,
                         gsl::span<const UnpaddedPieceSize>(exist_pieces),
                         PaddedPieceSize(sector_size_).unpadded(),
                         _,
                         0))
        .WillOnce(testing::Return(outcome::success(info)));

    api_->StateMinerInfo =
        [miner{miner_addr_}, spt{seal_proof_type_}](
            const Address &address,
            const TipsetKey &key) -> outcome::result<MinerInfo> {
      if (address == miner and key == TipsetKey{}) {
        MinerInfo minfo;
        minfo.seal_proof_type = spt;
        return std::move(minfo);
      }

      return ERROR_TEXT("ERROR");
    };

    ASSERT_EQ(sealing_->getListSectors().size(), 1);
    EXPECT_OUTCOME_TRUE_1(sealing_->pledgeSector());
    ASSERT_EQ(sealing_->getListSectors().size(), 2);
  }

}  // namespace fc::mining
