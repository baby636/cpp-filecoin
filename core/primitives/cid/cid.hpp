/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <spdlog/fmt/fmt.h>
#include <libp2p/multi/content_identifier.hpp>

#include "common/blob.hpp"
#include "common/buffer.hpp"
#include "common/outcome.hpp"
#include "primitives/cid/cid_prefix.hpp"

namespace fc {
  using common::Hash256;

  class CID : public libp2p::multi::ContentIdentifier {
   public:
    using ContentIdentifier::ContentIdentifier;

    using Multicodec = libp2p::multi::MulticodecType::Code;

    /**
     * ContentIdentifier is not default-constructable, but in some cases we need
     * default value. This value can be used to initialize class member or local
     * variable. Trying to CBOR encode this value will yield error, to ensure
     * proper initialization.
     */
    CID();

    explicit CID(const ContentIdentifier &cid);

    explicit CID(ContentIdentifier &&cid) noexcept;

    CID(CID &&cid) noexcept;

    CID(const CID &cid) = default;

    CID(Version version,
        Multicodec content_type,
        libp2p::multi::Multihash content_address);

    ~CID() = default;

    CID &operator=(const CID &) = default;

    CID &operator=(CID &&cid) noexcept;

    CID &operator=(const ContentIdentifier &cid);

    CID &operator=(ContentIdentifier &&cid);

    /**
     * Get cid prefix
     * @return
     */
    CidPrefix getPrefix() const;

    /**
     * @brief string-encodes cid
     * @return encoded value or error
     */
    outcome::result<std::string> toString() const;

    /**
     * @brief encodes CID to bytes
     * @return byte-representation of CID
     */
    outcome::result<std::vector<uint8_t>> toBytes() const;

    static outcome::result<CID> fromString(const std::string &str);

    static outcome::result<CID> fromBytes(gsl::span<const uint8_t> input);

    static outcome::result<CID> read(gsl::span<const uint8_t> &input,
                                     bool prefix = false);
  };

  size_t hash_value(const CID &cid);

  bool isCbor(const CID &cid);
  boost::optional<Hash256> asBlake(const CID &cid);
  boost::optional<BytesIn> asIdentity(const CID &cid);
  CID asCborBlakeCid(const Hash256 &hash);
}  // namespace fc

namespace std {
  template <>
  struct hash<fc::CID> {
    size_t operator()(const fc::CID &cid) const {
      return hash_value(cid);
    }
  };
}  // namespace std

template <>
struct fmt::formatter<fc::CID> : formatter<std::string_view> {
  template <typename C>
  auto format(const fc::CID &cid, C &ctx) {
    OUTCOME_EXCEPT(str, cid.toString());
    return formatter<std::string_view>::format(str, ctx);
  }
};

namespace fc::common {
  /// Compute CID from bytes
  outcome::result<CID> getCidOf(gsl::span<const uint8_t> bytes);

}  // namespace fc::common
