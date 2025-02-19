/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "storage/amt/amt.hpp"

#include <gtest/gtest.h>

#include "codec/cbor/light_reader/amt_walk.hpp"
#include "storage/ipfs/impl/in_memory_datastore.hpp"
#include "testutil/cbor.hpp"
#include "testutil/storage/ipld2.hpp"

using fc::codec::cbor::encode;
using fc::common::which;
using fc::storage::amt::Amt;
using fc::storage::amt::AmtError;
using fc::storage::amt::Node;
using fc::storage::amt::Root;
using fc::storage::amt::Value;
using fc::storage::ipfs::InMemoryDatastore;
using fc::storage::ipld::IpldIpld2;

class AmtTest : public ::testing::Test {
 public:
  auto getRoot() {
    return store->getCbor<Root>(amt.flush().value()).value();
  }

  std::shared_ptr<InMemoryDatastore> store{
      std::make_shared<InMemoryDatastore>()};
  Amt amt{store};
};

/** Amt node CBOR encoding and decoding */
TEST_F(AmtTest, NodeCbor) {
  Root r;
  r.height = 1;
  r.count = 2;
  r.node.bits_bytes = 1;
  expectEncodeAndReencode(r, "8301028341008080"_unhex);

  Node n;
  n.bits_bytes = 1;
  expectEncodeAndReencode(n, "8341008080"_unhex);

  n.items = Node::Values{{2, Value{"01"_unhex}}};
  expectEncodeAndReencode(n, "834104808101"_unhex);

  n.items = Node::Links{{3, "010000020000"_cid}};
  expectEncodeAndReencode(n, "83410881d82a470001000002000080"_unhex);

  n.items = Node::Links{{3, Node::Ptr{}}};
  EXPECT_OUTCOME_ERROR(AmtError::kExpectedCID, encode(n));
}

TEST_F(AmtTest, SetRemoveRootLeaf) {
  auto key = 3llu;
  auto value = Value{"07"_unhex};

  EXPECT_OUTCOME_ERROR(AmtError::kNotFound, amt.get(key));
  EXPECT_OUTCOME_ERROR(AmtError::kNotFound, amt.remove(key));
  EXPECT_OUTCOME_EQ(amt.count(), 0);

  EXPECT_OUTCOME_TRUE_1(amt.set(key, value));
  EXPECT_OUTCOME_EQ(amt.get(key), value);
  EXPECT_OUTCOME_EQ(amt.count(), 1);

  EXPECT_OUTCOME_TRUE_1(amt.remove(key));
  EXPECT_OUTCOME_ERROR(AmtError::kNotFound, amt.get(key));
  EXPECT_OUTCOME_EQ(amt.count(), 0);
}

TEST_F(AmtTest, SetRemoveCollapseZero) {
  auto key = 64;

  EXPECT_OUTCOME_TRUE_1(amt.set(1, "06"_unhex));
  EXPECT_TRUE(which<Node::Values>(getRoot().node.items));

  EXPECT_OUTCOME_TRUE_1(amt.set(key, "07"_unhex));
  EXPECT_FALSE(which<Node::Values>(getRoot().node.items));

  EXPECT_OUTCOME_TRUE_1(amt.remove(key));
  EXPECT_TRUE(which<Node::Values>(getRoot().node.items));
}

TEST_F(AmtTest, SetOverwrite) {
  auto key = 3;
  auto value1 = Value{"01"_unhex};
  auto value2 = Value{"02"_unhex};
  EXPECT_OUTCOME_TRUE_1(amt.set(key, value1));
  EXPECT_OUTCOME_EQ(amt.get(key), value1);
  EXPECT_OUTCOME_TRUE_1(amt.set(key, value2));
  EXPECT_OUTCOME_EQ(amt.get(key), value2);
}

TEST_F(AmtTest, Flush) {
  auto key = 9llu;
  auto value = Value{"07"_unhex};

  EXPECT_OUTCOME_TRUE_1(amt.set(key, value));
  EXPECT_OUTCOME_TRUE(cid, amt.flush());

  amt = {store, cid};
  EXPECT_OUTCOME_EQ(amt.get(key), value);
}

class AmtVisitTest : public AmtTest {
 public:
  AmtVisitTest() : AmtTest{} {
    for (auto &[key, value] : items) {
      EXPECT_OUTCOME_TRUE_1(amt.set(key, value));
    }
  }

  std::vector<std::pair<int64_t, Value>> items{{3, Value{"06"_unhex}},
                                               {64, Value{"07"_unhex}}};
};

TEST_F(AmtVisitTest, VisitWithoutFlush) {
  auto i = 0;
  EXPECT_OUTCOME_TRUE_1(amt.visit([&](uint64_t key, const Value &value) {
    EXPECT_EQ(key, items[i].first);
    EXPECT_EQ(value, items[i].second);
    ++i;
    return fc::outcome::success();
  }));
  EXPECT_EQ(i, items.size());
}

TEST_F(AmtVisitTest, VisitAfterFlush) {
  EXPECT_OUTCOME_TRUE_1(amt.flush());

  auto i = 0;
  EXPECT_OUTCOME_TRUE_1(amt.visit([&](uint64_t key, const Value &value) {
    EXPECT_EQ(key, items[i].first);
    EXPECT_EQ(value, items[i].second);
    ++i;
    return fc::outcome::success();
  }));
  EXPECT_EQ(i, items.size());

  EXPECT_OUTCOME_ERROR(AmtError::kIndexTooBig,
                       amt.visit([](uint64_t, const Value &) {
                         return AmtError::kIndexTooBig;
                       }));
}

TEST_F(AmtVisitTest, VisitError) {
  EXPECT_OUTCOME_ERROR(AmtError::kIndexTooBig,
                       amt.visit([](uint64_t, const Value &) {
                         return AmtError::kIndexTooBig;
                       }));
}

/*
 * walk visits amt values
 */
TEST_F(AmtVisitTest, Walk) {
  using namespace fc;
  codec::cbor::light_reader::AmtWalk walk{std::make_shared<IpldIpld2>(store),
                                          *asBlake(amt.flush().value())};
  BytesIn value;
  EXPECT_TRUE(walk.load());
  EXPECT_FALSE(walk.empty());
  EXPECT_TRUE(walk.next(value));
  EXPECT_FALSE(walk.empty());
  EXPECT_EQ(value, BytesIn{items[0].second});
  EXPECT_TRUE(walk.next(value));
  EXPECT_TRUE(walk.empty());
  EXPECT_EQ(value, BytesIn{items[1].second});
  EXPECT_FALSE(walk.next(value));
}
