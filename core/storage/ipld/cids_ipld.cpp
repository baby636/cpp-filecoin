/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "storage/ipld/cids_ipld.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/filesystem/operations.hpp>
#include "codec/uvarint.hpp"
#include "common/error_text.hpp"
#include "common/logger.hpp"
#include "common/outcome_fmt.hpp"
#include "common/ptr.hpp"

namespace fc::storage::ipld {
  using cids_index::kCborBlakePrefix;
  using cids_index::maxSize64;
  using cids_index::MergeRange;

  boost::optional<Row> CidsIpld::findWritten(const Key &key) const {
    assert(writable.is_open());
    auto it{written.lower_bound(Row{key, {}, {}})};
    if (it != written.end() && it->key == key) {
      return *it;
    }
    return boost::none;
  }

  outcome::result<bool> CidsIpld::contains(const CID &cid) const {
    if (auto key{asBlake(cid)}) {
      if (has(*key)) {
        return true;
      }
    }
    if (ipld) {
      return ipld->contains(cid);
    }
    return false;
  }

  outcome::result<void> CidsIpld::set(const CID &cid, Buffer value) {
    if (auto key{asBlake(cid)}) {
      if (writable.is_open()) {
        put(*key, value);
        return outcome::success();
      }
    }
    if (ipld) {
      OUTCOME_TRY(has, ipld->contains(cid));
      if (has) {
        return outcome::success();
      }
      return ipld->set(cid, std::move(value));
    }
    return ERROR_TEXT("CidsIpld.set: no ipld set");
  }

  outcome::result<Buffer> CidsIpld::get(const CID &cid) const {
    if (auto key{asBlake(cid)}) {
      Buffer value;
      if (get(*key, value)) {
        return std::move(value);
      }
    }
    if (ipld) {
      return ipld->get(cid);
    }
    return ipfs::IpfsDatastoreError::kNotFound;
  }

  Outcome<void> CidsIpld::doFlush() {
    std::shared_lock written_slock{written_mutex};
    uint64_t max_offset{};
    std::vector<Row> rows;
    rows.reserve(written.size());
    for (const auto &row : written) {
      max_offset = std::max(max_offset, row.offset.value());
      rows.push_back(row);
    }
    written_slock.unlock();
    std::sort(rows.begin(), rows.end());

    std::ifstream index_in{index_path, std::ios::binary};
    std::vector<MergeRange> ranges;
    auto &range1{ranges.emplace_back()};
    range1.begin = 1;
    range1.end = 1 + index->size();
    range1.file = &index_in;
    auto &range2{ranges.emplace_back()};
    range2.current = 0;
    range2.rows = std::move(rows);
    auto tmp_path{index_path + ".tmp"};
    std::ofstream index_out{tmp_path, std::ios::binary};
    OUTCOME_TRY(merge(index_out, std::move(ranges)));

    OUTCOME_TRY(new_index, cids_index::load(tmp_path, max_memory));
    std::unique_lock index_lock{index_mutex};
    boost::system::error_code ec;
    boost::filesystem::rename(tmp_path, index_path, ec);
    if (ec) {
      return ec;
    }
    index = new_index;
    index_lock.unlock();

    std::unique_lock written_ulock{written_mutex};
    for (auto it{written.begin()}; it != written.end();) {
      if (it->offset.value() > max_offset) {
        ++it;
      } else {
        it = written.erase(it);
      }
    }
    written_ulock.unlock();

    flushing.clear();

    return outcome::success();
  }

  bool CidsIpld::get(const Hash256 &key, Buffer *value) const {
    if (value) {
      value->resize(0);
    }
    std::shared_lock index_lock{index_mutex};
    auto row{index->find(key).value()};
    index_lock.unlock();
    if (!row && writable.is_open()) {
      std::shared_lock written_lock{written_mutex};
      row = findWritten(key);
    }
    if (!row) {
      return false;
    }
    if (value) {
      std::unique_lock car_lock{car_mutex};
      auto [good, size]{readCarItem(car_file, *row, nullptr)};
      if (!good) {
        spdlog::error("CidsIpld.get inconsistent");
        outcome::raise(ERROR_TEXT("CidsIpld.get: inconsistent"));
      }
      value->resize(size);
      if (!common::read(car_file, *value)) {
        spdlog::error("CidsIpld.get read error");
        outcome::raise(ERROR_TEXT("CidsIpld.get: read error"));
      }
    }
    return true;
  }

  void CidsIpld::put(const Hash256 &key, BytesIn value) {
    if (!writable.is_open()) {
      outcome::raise(ERROR_TEXT("CidsIpld.put: not writable"));
    }
    if (has(key)) {
      return;
    }
    std::unique_lock written_lock{written_mutex};
    if (findWritten(key)) {
      return;
    }

    Buffer item;
    codec::uvarint::VarintEncoder varint{kCborBlakePrefix.size() + key.size()
                                         + value.size()};
    item.reserve(varint.length + varint.value);
    item.put(varint.bytes());
    item.put(kCborBlakePrefix);
    item.put(key);
    item.put(value);

    Row row;
    row.key = key;
    row.offset = car_offset;
    row.max_size64 = maxSize64(item.size());
    if (!common::write(writable, item)) {
      spdlog::error("CidsIpld.put write error");
      outcome::raise(ERROR_TEXT("CidsIpld.put: write error"));
    }
    if (!writable.flush().good()) {
      spdlog::error("CidsIpld.put flush error");
      outcome::raise(ERROR_TEXT("CidsIpld.put: flush error"));
    }
    car_offset += item.size();
    written.insert(row);
    if (flush_on && written.size() >= flush_on) {
      written_lock.unlock();
      asyncFlush();
    }
  }

  void CidsIpld::asyncFlush() {
    if (!flushing.test_and_set()) {
      if (io) {
        io->post(weakCb0(weak_from_this(), [this] {
          if (auto r{doFlush()}; !r) {
            spdlog::error("CidsIpld({}) async flush: {:#}", index_path, ~r);
          }
        }));
      } else {
        if (auto r{doFlush()}; !r) {
          spdlog::error("CidsIpld({}) flush: {:#}", index_path, ~r);
        }
      }
    }
  }

  outcome::result<bool> Ipld2Ipld::contains(const CID &cid) const {
    return ipld->has(*asBlake(cid));
  }

  outcome::result<void> Ipld2Ipld::set(const CID &cid, Buffer value) {
    ipld->put(*asBlake(cid), value);
    return outcome::success();
  }

  outcome::result<Buffer> Ipld2Ipld::get(const CID &cid) const {
    Buffer value;
    if (!ipld->get(*asBlake(cid), value)) {
      return ipfs::IpfsDatastoreError::kNotFound;
    }
    return std::move(value);
  }
}  // namespace fc::storage::ipld
