#pragma once

#include "auxiliaryFunctions.hh"

namespace simeng {
namespace arch {
namespace aarch64 {
class sveHelp {
 public:
  /** Helper function for SVE instructions with the format `add zd, zn, zm`. */
  template <typename T>
  static RegisterValue sveAdd_3ops(
      std::array<RegisterValue, Instruction::MAX_SOURCE_REGISTERS>& operands,
      const uint16_t VL_bits) {
    const T* n = operands[0].getAsVector<T>();
    const T* m = operands[1].getAsVector<T>();

    const uint16_t partition_num = VL_bits / (sizeof(T) * 8);
    T out[256 / sizeof(T)] = {0};
    for (int i = 0; i < partition_num; i++) {
      out[i] = n[i] + m[i];
    }
    return {out, 256};
  }

  /** Helper function for SVE instructions with the format `add zdn, pg/m, zdn,
   * const`. */
  template <typename T>
  static RegisterValue sveAddPredicated_const(
      std::array<RegisterValue, Instruction::MAX_SOURCE_REGISTERS>& operands,
      const simeng::arch::aarch64::InstructionMetadata& metadata,
      const uint16_t VL_bits) {
    bool isFP = std::is_floating_point<T>::value;
    const uint64_t* p = operands[0].getAsVector<uint64_t>();
    const T* d = operands[1].getAsVector<T>();
    const auto con = isFP ? metadata.operands[3].fp : metadata.operands[3].imm;

    const uint16_t partition_num = VL_bits / (sizeof(T) * 8);
    T out[256 / sizeof(T)] = {0};
    for (int i = 0; i < partition_num; i++) {
      uint64_t shifted_active = 1ull << ((i % (64 / sizeof(T))) * sizeof(T));
      if (p[i / (64 / sizeof(T))] & shifted_active)
        out[i] = d[i] + con;
      else
        out[i] = d[i];
    }
    return {out, 256};
  }

  /** Helper function for SVE instructions with the format `add zdn, pg/m, zdn,
   * zm`. */
  template <typename T>
  static RegisterValue sveAddPredicated_vecs(
      std::array<RegisterValue, Instruction::MAX_SOURCE_REGISTERS>& operands,
      const uint16_t VL_bits) {
    const uint64_t* p = operands[0].getAsVector<uint64_t>();
    const T* d = operands[1].getAsVector<T>();
    const T* m = operands[2].getAsVector<T>();

    const uint16_t partition_num = VL_bits / (sizeof(T) * 8);
    T out[256 / sizeof(T)] = {0};
    for (int i = 0; i < partition_num; i++) {
      uint64_t shifted_active = 1ull << ((i % (64 / sizeof(T))) * sizeof(T));
      if (p[i / (64 / sizeof(T))] & shifted_active)
        out[i] = d[i] + m[i];
      else
        out[i] = d[i];
    }
    return {out, 256};
  }

  /** Helper function for instructions with the format `cmp<eq, ge, gt, hi, hs,
   *le, lo, ls, lt, ne> pd, pg/z, zn, <zm, #imm>`. */
  template <typename T>
  static std::tuple<std::array<uint64_t, 4>, uint8_t> sveCmpPredicated_toPred(
      std::array<RegisterValue, Instruction::MAX_SOURCE_REGISTERS>& operands,
      const simeng::arch::aarch64::InstructionMetadata& metadata,
      const uint16_t VL_bits, bool cmpToImm, std::function<bool(T, T)> func) {
    const uint64_t* p = operands[0].getAsVector<uint64_t>();
    const T* n = operands[1].getAsVector<T>();
    const T* m;
    T imm;
    if (cmpToImm)
      imm = static_cast<T>(metadata.operands[3].imm);
    else
      m = operands[2].getAsVector<T>();

    const uint16_t partition_num = VL_bits / (sizeof(T) * 8);
    std::array<uint64_t, 4> out = {0, 0, 0, 0};

    for (int i = 0; i < partition_num; i++) {
      uint64_t shifted_active = 1ull << ((i % (64 / sizeof(T))) * sizeof(T));
      if (p[i / (64 / sizeof(T))] & shifted_active) {
        if (cmpToImm)
          out[i / (64 / sizeof(T))] |= (func(n[i], imm)) ? (shifted_active) : 0;
        else
          out[i / (64 / sizeof(T))] |=
              (func(n[i], m[i])) ? (shifted_active) : 0;
      }
    }
    // Byte count = sizeof(P) as destination predicate is predicate of P bytes.
    return {out, AuxFunc::getNZCVfromPred(out, VL_bits, sizeof(T))};
  }

  /** Helper function for SVE instructions with the format `cntp xd, pg, pn`. */
  template <typename T>
  static uint64_t sveCntp(
      std::array<RegisterValue, Instruction::MAX_SOURCE_REGISTERS>& operands,
      const uint16_t VL_bits) {
    const uint64_t* pg = operands[0].getAsVector<uint64_t>();
    const uint64_t* pn = operands[1].getAsVector<uint64_t>();

    const uint16_t partition_num = VL_bits / (sizeof(T) * 8);
    uint64_t count = 0;

    for (int i = 0; i < partition_num; i++) {
      uint64_t shifted_active = 1ull << ((i % (64 / sizeof(T))) * sizeof(T));
      if (pg[i / (64 / sizeof(T))] & shifted_active) {
        count += (pn[i / (64 / sizeof(T))] & shifted_active) ? 1 : 0;
      }
    }
    return count;
  }

  /** Helper function for SVE instructions with the format `dec<b,d,h,s> xdn{,
   * pattern{, MUL #imm}}`. */
  // TODO : Add support for patterns
  template <typename T>
  static uint64_t sveDec_scalar(
      std::array<RegisterValue, Instruction::MAX_SOURCE_REGISTERS>& operands,
      const simeng::arch::aarch64::InstructionMetadata& metadata,
      const uint16_t VL_bits) {
    const uint64_t n = operands[0].get<uint64_t>();
    const uint8_t imm = static_cast<uint8_t>(metadata.operands[1].imm);
    return (n - ((VL_bits / (sizeof(T) * 8)) * imm));
  }

  /** Helper function for SVE instructions with the format `dup zd, <#imm{,
   * shift}, <w,x>n>`. */
  template <typename T>
  static RegisterValue sveDup_immOrScalar(
      std::array<RegisterValue, Instruction::MAX_SOURCE_REGISTERS>& operands,
      const simeng::arch::aarch64::InstructionMetadata& metadata,
      const uint16_t VL_bits, bool useImm) {
    bool isFP = std::is_floating_point<T>::value;
    const auto imm =
        useImm ? (isFP ? metadata.operands[1].fp
                       : static_cast<int8_t>(metadata.operands[1].imm))
               : operands[0].get<T>();
    const uint16_t partition_num = VL_bits / (sizeof(T) * 8);
    T out[256 / sizeof(T)] = {0};

    for (int i = 0; i < partition_num; i++) {
      out[i] = imm;
    }
    return {out, 256};
  }

  /** Helper function for SVE instructions with the format `dup zd, zn[#imm]`.
   */
  template <typename T>
  static RegisterValue sveDup_vecIndexed(
      std::array<RegisterValue, Instruction::MAX_SOURCE_REGISTERS>& operands,
      const simeng::arch::aarch64::InstructionMetadata& metadata,
      const uint16_t VL_bits) {
    const uint16_t index =
        static_cast<uint16_t>(metadata.operands[1].vector_index);
    const T* n = operands[0].getAsVector<T>();

    const uint16_t partition_num = VL_bits / (sizeof(T) * 8);
    T out[256 / sizeof(T)] = {0};

    if (index < (VL_bits / (sizeof(T) * 8))) {
      const T element = n[index];
      for (int i = 0; i < partition_num; i++) {
        out[i] = element;
      }
    }
    return {out, 256};
  }

  /** Helper function for SVE instructions with the format `fabs zd,
   * pg/z, zn`. T represents the vector register type (i.e. zd.b would be
   * uint8_t).*/
  template <typename T>
  static RegisterValue sveFabsPredicated(
      std::array<RegisterValue, Instruction::MAX_SOURCE_REGISTERS>& operands,
      const uint16_t VL_bits) {
    const T* d = operands[0].getAsVector<T>();
    const uint64_t* p = operands[1].getAsVector<uint64_t>();
    const T* n = operands[2].getAsVector<T>();

    const uint16_t partition_num = VL_bits / (sizeof(T) * 8);
    T out[256 / sizeof(T)] = {0};

    for (int i = 0; i < partition_num; i++) {
      uint64_t shifted_active = 1ull << ((i % (64 / sizeof(T))) * sizeof(T));
      if (p[i / (64 / sizeof(T))] & shifted_active) {
        out[i] = ::fabs(n[i]);
      } else {
        out[i] = d[i];
      }
    }
    return {out, 256};
  }

  /** Helper function for SVE instructions with the format `fadda rd,
   * pg/m, rn, zm`. T represents the vector register type (i.e. zd.b would be
   * uint8_t).*/
  template <typename T>
  static RegisterValue sveFaddaPredicated(
      std::array<RegisterValue, Instruction::MAX_SOURCE_REGISTERS>& operands,
      const uint16_t VL_bits) {
    const uint64_t* p = operands[1].getAsVector<uint64_t>();
    const T n = operands[2].get<T>();
    const T* m = operands[3].getAsVector<T>();

    const uint16_t partition_num = VL_bits / (sizeof(T) * 8);
    T out[256 / sizeof(T)] = {0};
    out[0] = n;

    for (int i = 0; i < partition_num; i++) {
      uint64_t shifted_active = 1ull << ((i % (64 / sizeof(T))) * sizeof(T));
      if (p[i / (64 / sizeof(T))] & shifted_active) {
        out[0] += m[i];
      }
    }
    return {out, 256};
  }

  /** Helper function for SVE instructions with the format `fcvt zd,
   * pg/m, zn`.
   * D represents the destination vector register type (i.e. zd.s would be
   * int32_t).
   * N represents the source vector register type (i.e. zn.d would be double).
   */
  template <typename D, typename N>
  static RegisterValue sveFcvtPredicated(
      std::array<RegisterValue, Instruction::MAX_SOURCE_REGISTERS>& operands,
      const uint16_t VL_bits) {
    const D* d = operands[0].getAsVector<D>();
    const uint64_t* p = operands[1].getAsVector<uint64_t>();
    const N* n = operands[2].getAsVector<N>();

    // Stores size of largest type out of D and N
    int lts = std::max(sizeof(D), sizeof(N));
    bool sourceLarger = (sizeof(D) < sizeof(N)) ? true : false;

    const uint16_t partition_num = VL_bits / (lts * 8);
    D out[256 / sizeof(D)] = {0};

    for (int i = 0; i < partition_num; i++) {
      uint64_t shifted_active = 1ull << ((i % (64 / lts)) * lts);
      int indexOut = (sourceLarger) ? (2 * i) : i;
      int indexN = (!sourceLarger) ? (2 * i) : i;

      if (p[i / (64 / lts)] & shifted_active) {
        if (n[indexN] > std::numeric_limits<D>::max())
          out[indexOut] = std::numeric_limits<D>::max();
        else if (n[indexN] < std::numeric_limits<D>::lowest())
          out[indexOut] = std::numeric_limits<D>::lowest();
        else
          out[indexOut] = static_cast<D>(n[indexN]);
      } else {
        out[indexOut] = d[indexOut];
      }
      if (sourceLarger) out[indexOut + 1] = d[indexOut + 1];
    }
    return {out, 256};
  }

  /** Helper function for SVE instructions with the format `fcvtzs zd,
   * pg/m, zn`.
   * D represents the destination vector register type (i.e. zd.s would be
   * int32_t).
   * N represents the source vector register type (i.e. zn.d would be double).
   */
  template <typename D, typename N>
  static RegisterValue sveFcvtzsPredicated(
      std::array<RegisterValue, Instruction::MAX_SOURCE_REGISTERS>& operands,
      const uint16_t VL_bits) {
    const D* d = operands[0].getAsVector<D>();
    const uint64_t* p = operands[1].getAsVector<uint64_t>();
    const N* n = operands[2].getAsVector<N>();

    // Stores size of largest type out of D and N
    int lts = std::max(sizeof(D), sizeof(N));
    bool sameType = (sizeof(D) == sizeof(N)) ? true : false;
    bool sourceLarger = (sizeof(D) < sizeof(N)) ? true : false;

    const uint16_t partition_num = VL_bits / (lts * 8);
    D out[256 / sizeof(D)] = {0};

    for (int i = 0; i < partition_num; i++) {
      uint64_t shifted_active = 1ull << ((i % (64 / lts)) * lts);
      int indexOut = (sourceLarger) ? (2 * i) : i;
      int indexN = ((!sourceLarger) & (!sameType)) ? (2 * i) : i;

      if (p[i / (64 / lts)] & shifted_active) {
        if (n[indexN] > std::numeric_limits<D>::max())
          out[indexOut] = std::numeric_limits<D>::max();
        else if (n[indexN] < std::numeric_limits<D>::lowest())
          out[indexOut] = std::numeric_limits<D>::lowest();
        else
          out[indexOut] = static_cast<D>(std::trunc(n[indexN]));
        // Can be set to 0xFFFFFFFF as will only occur when D=int32_t.
        if (sourceLarger) out[indexOut + 1] = (n[indexN] < 0) ? 0xFFFFFFFFu : 0;
      } else {
        out[indexOut] = d[indexOut];
        if (sourceLarger) out[indexOut + 1] = d[indexOut + 1];
      }
    }
    return {out, 256};
  }

  /** Helper function for SVE instructions with the format `fcm<ge, lt,...> pd,
   * pg/z, zn, zm`. T represents the vector register type (i.e. pd.b would be
   * uint8_t).*/
  template <typename T>
  static std::array<uint64_t, 4> sveComparePredicated_vecsToPred(
      std::array<RegisterValue, Instruction::MAX_SOURCE_REGISTERS>& operands,
      const simeng::arch::aarch64::InstructionMetadata& metadata,
      const uint16_t VL_bits, bool cmpToZero, std::function<bool(T, T)> func) {
    const uint64_t* p = operands[0].getAsVector<uint64_t>();
    const T* n = operands[1].getAsVector<T>();
    const T* m;
    if (!cmpToZero) m = operands[2].getAsVector<T>();

    const uint16_t partition_num = VL_bits / (sizeof(T) * 8);
    std::array<uint64_t, 4> out = {0};

    for (int i = 0; i < partition_num; i++) {
      uint64_t shifted_active = 1ull << ((i % (64 / sizeof(T))) * sizeof(T));
      if (p[i / (64 / sizeof(T))] & shifted_active) {
        out[i / (64 / sizeof(T))] |=
            (func(n[i], cmpToZero ? 0.0 : m[i])) ? shifted_active : 0;
      }
    }
    return out;
  }

  /** Helper function for SVE instructions with the format `index zd, #imm,
   * #imm`. T represents the vector register type (i.e. zd.b would be
   * int8_t).*/
  template <typename T>
  static RegisterValue sveIndex_2imm(
      const simeng::arch::aarch64::InstructionMetadata& metadata,
      const uint16_t VL_bits) {
    const T imm1 = static_cast<T>(metadata.operands[1].imm);
    const T imm2 = static_cast<T>(metadata.operands[2].imm);

    const uint16_t partition_num = VL_bits / (sizeof(T) * 8);
    T out[256 / sizeof(T)] = {0};

    for (int i = 0; i < partition_num; i++) {
      out[i] = static_cast<T>(imm1 + (i * imm2));
    }
    return {out, 256};
  }

  /** Helper function for SVE instructions with the format `<AND, EOR, ...> pd,
   * pg/z, pn, pm`. T represents the vector register type (i.e. pd.b would be
   * uint8_t).*/
  template <typename T>
  static std::array<uint64_t, 4> sveLogicOp_preds(
      std::array<RegisterValue, Instruction::MAX_SOURCE_REGISTERS>& operands,
      const uint16_t VL_bits,
      std::function<uint64_t(uint64_t, uint64_t)> func) {
    const uint64_t* p = operands[0].getAsVector<uint64_t>();
    const uint64_t* n = operands[1].getAsVector<uint64_t>();
    const uint64_t* m = operands[2].getAsVector<uint64_t>();

    const uint16_t partition_num = VL_bits / (sizeof(T) * 8);
    std::array<uint64_t, 4> out = {0};

    for (int i = 0; i < partition_num; i++) {
      uint64_t shifted_active = 1ull << ((i % (64 / sizeof(T))) * sizeof(T));
      if (p[i / (64 / sizeof(T))] & shifted_active) {
        out[i / (64 / sizeof(T))] |=
            (func(n[i / (64 / sizeof(T))], m[i / (64 / sizeof(T))]) &
             shifted_active);
      }
    }
    return out;
  }

  /** Helper function for SVE instructions with the format `<AND, EOR, ...> zd,
   * pg/z, zn, zm`. T represents the vector register type (i.e. zd.b would be
   * uint8_t).*/
  template <typename T>
  static RegisterValue sveLogicOpPredicated_vecs(
      std::array<RegisterValue, Instruction::MAX_SOURCE_REGISTERS>& operands,
      const uint16_t VL_bits,
      std::function<uint64_t(uint64_t, uint64_t)> func) {
    const uint64_t* p = operands[0].getAsVector<uint64_t>();
    const T* dn = operands[1].getAsVector<T>();
    const T* m = operands[2].getAsVector<T>();

    const uint16_t partition_num = VL_bits / (sizeof(T) * 8);
    T out[256 / sizeof(T)] = {0};
    for (int i = 0; i < partition_num; i++) {
      uint64_t shifted_active = 1ull << ((i % (64 / sizeof(T))) * sizeof(T));
      if (p[i / (64 / sizeof(T))] & shifted_active)
        out[i] = func(dn[i], m[i]);
      else
        out[i] = dn[i];
    }
    return {out, 256};
  }

  /** Helper function for SVE instructions with the format `ptrue pd{, pattern}.
   * T represents the predicate type (i.e. pd.b would be uint8_t).
   */
  template <typename T>
  static std::array<uint64_t, 4> svePtrue(const uint16_t VL_bits) {
    const uint16_t partition_num = VL_bits / (sizeof(T) * 8);
    std::array<uint64_t, 4> out = {0, 0, 0, 0};

    for (int i = 0; i < partition_num; i++) {
      uint64_t shifted_active = 1ull << ((i % (64 / sizeof(T))) * sizeof(T));
      out[i / (64 / sizeof(T))] |= shifted_active;
    }
    return out;
  }

  /** Helper function for SVE instructions with the format `whilelo pd, <w,x>n,
   * <w,x>m`.
   * T represents the type of operands n and m (i.e. uint32_t for wn).
   * P represents the type of operand p (i.e. uint8_t for pd.b).
   */
  template <typename T, typename P>
  static std::tuple<std::array<uint64_t, 4>, uint8_t> sveWhilelo(
      std::array<RegisterValue, Instruction::MAX_SOURCE_REGISTERS>& operands,
      const uint16_t VL_bits, bool calcNZCV) {
    const T n = operands[0].get<T>();
    const T m = operands[1].get<T>();

    const uint16_t partition_num = VL_bits / (sizeof(P) * 8);
    std::array<uint64_t, 4> out = {0, 0, 0, 0};
    uint16_t index = 0;

    for (int i = 0; i < partition_num; i++) {
      // Determine whether lane should be active and shift to align with
      // element in predicate register.
      uint64_t shifted_active =
          (n + i) < m ? 1ull << ((i % (64 / (sizeof(P))) * (sizeof(P)))) : 0;
      out[index / (64 / (sizeof(P)))] =
          out[index / (64 / (sizeof(P)))] | shifted_active;
      index++;
    }
    // Byte count = sizeof(P) as destination predicate is predicate of P bytes.
    uint8_t nzcv =
        calcNZCV ? AuxFunc::getNZCVfromPred(out, VL_bits, sizeof(P)) : 0;
    return {out, nzcv};
  }
};
}  // namespace aarch64
}  // namespace arch
}  // namespace simeng