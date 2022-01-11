#pragma once

#include <cmath>
#include <limits>
#include <tuple>

#include "ExecuteHelperFunctions.hh"
#include "arch/aarch64/InstructionMetadata.hh"

namespace simeng {
namespace arch {
namespace aarch64 {
class neonHelp {
 public:
  /** Helper function for NEON instructions with the format `add vd, vn, vm`.
   * I represents the number of elements in the output array to be updated (i.e.
   * for vd.8b the final 8 elements in the output array will be 0).
   */
  template <typename T, int I>
  static std::array<T, (16 / sizeof(T))> vecAdd_3ops(
      std::array<RegisterValue, Instruction::MAX_SOURCE_REGISTERS> operands) {
    const T* n = operands[0].getAsVector<T>();
    const T* m = operands[1].getAsVector<T>();
    std::array<T, (16 / sizeof(T))> out = {0};
    for (int i = 0; i < I; i++) {
      out[i] = static_cast<T>(n[i] + m[i]);
    }
    return out;
  }

  /** Helper function for NEON instructions with the format `addp rd, vn`.
   * I represents the number of elements in the input array to be summed.
   */
  template <typename T, int I>
  static T vecSumElems_2ops(
      std::array<RegisterValue, Instruction::MAX_SOURCE_REGISTERS> operands) {
    const T* n = operands[0].getAsVector<T>();
    T result = 0;
    for (int i = 0; i < I; i++) {
      result += n[i];
    }
    return result;
  }

  /** Helper function for NEON instructions with the format `addp vd, vn, vm`.
   * I represents the number of elements in the output array to be updated (i.e.
   * for vd.8b the final 8 elements in the output array will be 0).
   */
  template <typename T, int I>
  static std::array<T, (16 / sizeof(T))> vecAddp_3ops(
      std::array<RegisterValue, Instruction::MAX_SOURCE_REGISTERS> operands) {
    const T* n = operands[0].getAsVector<T>();
    const T* m = operands[1].getAsVector<T>();
    std::array<T, (16 / sizeof(T))> out = {0};
    uint8_t offset = I / 2;
    for (int i = 0; i < I; i++) {
      if (i < offset) {
        out[i] = static_cast<T>(n[i * 2] + n[(i * 2) + 1]);
      } else {
        out[i] =
            static_cast<T>(m[(i - offset) * 2] + m[((i - offset) * 2) + 1]);
      }
    }
    return out;
  }
};
}  // namespace aarch64
}  // namespace arch
}  // namespace simeng