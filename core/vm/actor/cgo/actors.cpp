/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "vm/actor/cgo/actors.hpp"

#include "proofs/impl/proof_engine_impl.hpp"
#include "vm/actor/builtin/types/storage_power/policy.hpp"
#include "vm/actor/cgo/c_actors.h"
#include "vm/actor/cgo/go_actors.h"
#include "vm/dvm/dvm.hpp"
#include "vm/runtime/env.hpp"
#include "vm/toolchain/toolchain.hpp"

#define RUNTIME_METHOD(name)                             \
  void rt_##name(const std::shared_ptr<Runtime> &,       \
                 CborDecodeStream &,                     \
                 CborEncodeStream &);                    \
  CBOR_METHOD(name) {                                    \
    rt_##name(runtimes.at(arg.get<size_t>()), arg, ret); \
  }                                                      \
  void rt_##name(const std::shared_ptr<Runtime> &rt,     \
                 CborDecodeStream &arg,                  \
                 CborEncodeStream &ret)

namespace fc::vm::actor::cgo {
  using builtin::types::storage_power::kConsensusMinerMinPower;
  using crypto::randomness::DomainSeparationTag;
  using crypto::randomness::Randomness;
  using primitives::ChainEpoch;
  using primitives::GasAmount;
  using primitives::TokenAmount;
  using primitives::address::Address;
  using primitives::piece::PieceInfo;
  using primitives::sector::RegisteredSealProof;
  using primitives::sector::SealVerifyInfo;
  using primitives::sector::WindowPoStVerifyInfo;
  using toolchain::Toolchain;

  void configParams() {
    CborEncodeStream arg;
    arg << kConsensusMinerMinPower;
    cgoCall<cgoActorsConfigParams>(arg);
  }

  constexpr auto kFatal{VMExitCode::kFatal};
  constexpr auto kOk{VMExitCode::kOk};

  static std::map<size_t, std::shared_ptr<Runtime>> runtimes;
  static size_t next_runtime{0};

  static std::shared_ptr<proofs::ProofEngine> proofs =
      std::make_shared<proofs::ProofEngineImpl>();

  outcome::result<Buffer> invoke(const CID &code,
                                 const std::shared_ptr<Runtime> &runtime) {
    CborEncodeStream arg;
    auto id{next_runtime++};  // TODO: mod
    auto message{runtime->getMessage().get()};
    auto version{runtime->getNetworkVersion()};
    arg << id << version << message.from << message.to
        << runtime->getCurrentEpoch() << message.value << code << message.method
        << message.params;
    runtimes.emplace(id, runtime);
    auto ret{cgoCall<cgoActorsInvoke>(arg)};
    runtimes.erase(id);
    auto exit{ret.get<VMExitCode>()};
    if (exit != kOk) {
      return exit;
    }
    return ret.get<Buffer>();
  }

  template <typename T>
  inline auto charge(CborEncodeStream &ret, const outcome::result<T> &r) {
    if (!r && r.error() == asAbort(VMExitCode::kSysErrOutOfGas)) {
      ret << VMExitCode::kSysErrOutOfGas;
      return true;
    }
    return false;
  }

  template <typename T>
  inline auto chargeFatal(CborEncodeStream &ret, const outcome::result<T> &r) {
    if (charge(ret, r)) {
      return true;
    }
    if (!r) {
      ret << kFatal;
      return true;
    }
    return false;
  }

  inline auto charge(CborEncodeStream &ret,
                     const std::shared_ptr<Runtime> &rt,
                     GasAmount gas) {
    return !chargeFatal(ret, rt->execution()->chargeGas(gas));
  }

  inline boost::optional<Buffer> ipldGet(CborEncodeStream &ret,
                                         const std::shared_ptr<Runtime> &rt,
                                         const CID &cid) {
    if (auto r{rt->execution()->charging_ipld->get(cid)}) {
      return std::move(r.value());
    } else {
      chargeFatal(ret, r);
    }
    return {};
  }

  inline boost::optional<CID> ipldPut(CborEncodeStream &ret,
                                      const std::shared_ptr<Runtime> &rt,
                                      BytesIn value) {
    OUTCOME_EXCEPT(cid, common::getCidOf(value));
    if (auto r{rt->execution()->charging_ipld->set(cid, Buffer{value})}) {
      return std::move(cid);
    } else {
      chargeFatal(ret, r);
    }
    return {};
  }

  RUNTIME_METHOD(gocRtIpldGet) {
    if (auto value{ipldGet(ret, rt, arg.get<CID>())}) {
      ret << kOk << *value;
    }
  }

  RUNTIME_METHOD(gocRtIpldPut) {
    auto buf = arg.get<Buffer>();
    if (auto cid{ipldPut(ret, rt, buf)}) {
      ret << kOk << *cid;
    }
  }

  RUNTIME_METHOD(gocRtCharge) {
    if (charge(ret, rt, arg.get<GasAmount>())) {
      ret << kOk;
    }
  }

  RUNTIME_METHOD(gocRtRandomnessFromTickets) {
    auto tag{arg.get<DomainSeparationTag>()};
    auto round{arg.get<ChainEpoch>()};
    auto seed{arg.get<Buffer>()};
    auto r = rt->getRandomnessFromTickets(tag, round, seed);
    if (!r) {
      ret << kFatal;
    } else {
      ret << kOk << r.value();
    }
  }

  RUNTIME_METHOD(gocRtRandomnessFromBeacon) {
    auto tag{arg.get<DomainSeparationTag>()};
    auto round{arg.get<ChainEpoch>()};
    auto seed{arg.get<Buffer>()};
    auto r = rt->getRandomnessFromBeacon(tag, round, seed);
    if (!r) {
      ret << kFatal;
    } else {
      ret << kOk << r.value();
    }
  }

  RUNTIME_METHOD(gocRtBlake) {
    auto data{arg.get<Buffer>()};
    auto hash{rt->hashBlake2b(data)};
    if (!chargeFatal(ret, hash)) {
      ret << kOk << hash.value();
    }
  }

  RUNTIME_METHOD(gocRtVerifyPost) {
    auto info{arg.get<WindowPoStVerifyInfo>()};
    if (charge(ret, rt, rt->execution()->env->pricelist.onVerifyPost(info))) {
      info.randomness[31] &= 0x3f;
      auto r{proofs->verifyWindowPoSt(info)};
      ret << kOk << (r && r.value());
    }
  }

  RUNTIME_METHOD(gocRtVerifySeals) {
    auto n{arg.get<size_t>()};
    ret << kOk;
    for (auto i{0u}; i < n; ++i) {
      auto r{proofs->verifySeal(arg.get<SealVerifyInfo>())};
      ret << (r && r.value());
    }
  }

  RUNTIME_METHOD(gocRtActorId) {
    if (auto _id{
            rt->execution()->state_tree->tryLookupId(arg.get<Address>())}) {
      if (auto &id{_id.value()}) {
        ret << kOk << true << *id;
      } else {
        ret << kOk << false;
      }
    } else {
      ret << kFatal;
    }
  }

  RUNTIME_METHOD(gocRtSend) {
    auto to = arg.get<Address>();
    auto method = arg.get<uint64_t>();
    auto params = arg.get<Buffer>();
    auto value = arg.get<TokenAmount>();
    auto r{rt->send(to, method, params, value)};
    if (!r) {
      auto &e{r.error()};
      if (!isVMExitCode(e) || e == kFatal) {
        ret << kFatal;
      } else {
        ret << kOk << e.value();

        dvm::onReceipt({VMExitCode{e.value()}, {}, rt->execution()->gas_used});
      }
    } else {
      ret << kOk << kOk << r.value();
    }
  }

  RUNTIME_METHOD(gocRtVerifySig) {
    auto signature_bytes{arg.get<Buffer>()};
    auto address{arg.get<Address>()};
    auto data{arg.get<Buffer>()};
    auto ok{rt->verifySignatureBytes(signature_bytes, address, data)};
    if (!chargeFatal(ret, ok)) {
      ret << kOk << ok.value();
    }
  }

  RUNTIME_METHOD(gocRtVerifyConsensusFault) {
    auto block1{arg.get<Buffer>()};
    auto block2{arg.get<Buffer>()};
    auto extra{arg.get<Buffer>()};
    auto _fault{rt->verifyConsensusFault(block1, block2, extra)};
    // TODO(turuslan): correct error handling
    if (!charge(ret, _fault)) {
      auto &fault{_fault.value()};
      if (fault) {
        ret << kOk << true << fault->target << fault->epoch << fault->type;
      } else {
        ret << kOk << false;
      }
    }
  }

  RUNTIME_METHOD(gocRtCommD) {
    auto type{arg.get<RegisteredSealProof>()};
    auto pieces{arg.get<std::vector<PieceInfo>>()};
    if (auto cid{rt->computeUnsealedSectorCid(type, pieces)}) {
      ret << kOk << true << cid.value();
    } else if (!charge(ret, cid)) {
      ret << kOk << false;
    }
  }

  RUNTIME_METHOD(gocRtNewAddress) {
    if (auto address{rt->createNewActorAddress()}) {
      ret << kOk << address.value();
    } else {
      ret << kFatal;
    }
  }

  RUNTIME_METHOD(gocRtCreateActor) {
    auto code{arg.get<CID>()};
    auto address{arg.get<Address>()};
    const auto address_matcher =
        toolchain::Toolchain::createAddressMatcher(rt->getActorVersion());
    if (!address_matcher->isBuiltinActor(code)
        || address_matcher->isSingletonActor(code)
        || rt->execution()->state_tree->get(address)) {
      ret << VMExitCode::kSysErrIllegalArgument;
    } else if (charge(
                   ret, rt, rt->execution()->env->pricelist.onCreateActor())) {
      if (rt->execution()->state_tree->set(
              address, {code, actor::kEmptyObjectCid, 0, 0})) {
        ret << kOk;
      } else {
        ret << kFatal;
      }
    }
  }

  RUNTIME_METHOD(gocRtActorCode) {
    if (auto _actor{rt->execution()->state_tree->tryGet(arg.get<Address>())}) {
      if (auto &actor{_actor.value()}) {
        ret << kOk << true << actor->code;
      } else {
        ret << kOk << false;
      }
    } else {
      ret << kFatal;
    }
  }

  RUNTIME_METHOD(gocRtActorBalance) {
    if (auto balance{rt->getBalance(rt->getMessage().get().to)}) {
      ret << kOk << balance.value();
    } else {
      ret << kFatal;
    }
  }

  RUNTIME_METHOD(gocRtStateGet) {
    if (auto _actor{
            rt->execution()->state_tree->get(rt->getMessage().get().to)}) {
      auto &head{_actor.value().head};
      if (auto state{ipldGet(ret, rt, head)}) {
        ret << kOk << true << *state;
        if (arg.get<bool>()) {
          ret << head;
        }
      }
    } else {
      ret << kOk << false;
    }
  }

  RUNTIME_METHOD(gocRtStateCommit) {
    if (auto cid{ipldPut(ret, rt, arg.get<Buffer>())}) {
      if (auto _actor{
              rt->execution()->state_tree->get(rt->getMessage().get().to)}) {
        auto &actor{_actor.value()};
        if (actor.head != arg.get<CID>()) {
          ret << kFatal;
        } else {
          actor.head = *cid;
          if (rt->execution()->state_tree->set(rt->getMessage().get().to,
                                               actor)) {
            ret << kOk;
          } else {
            ret << kFatal;
          }
        }
      } else {
        ret << kFatal;
      }
    }
  }

  RUNTIME_METHOD(gocRtDeleteActor) {
    if (rt->deleteActor(arg.get<Address>())) {
      ret << kOk;
    } else {
      ret << kFatal;
    }
  }

  RUNTIME_METHOD(gocRtCirc) {
    if (auto _amount{rt->getTotalFilCirculationSupply()}) {
      ret << kOk << _amount.value();
    } else {
      ret << VMExitCode::kErrIllegalState;
    }
  }
}  // namespace fc::vm::actor::cgo
