#pragma once

#include <cmath>
#include <limits>
#include <tuple>

#include "ExecuteHelperFunctions.hh"
#include "arch/aarch64/InstructionMetadata.hh"

namespace simeng {
namespace arch {
namespace aarch64 {
class bitmanipHelp {
 public:
  /** Helper function for instructions with the format `bfm rd, rn, #imr,
   * #imms`. Returns Single value. */
  template <typename T>
  static T bfm_2imms(
      std::array<RegisterValue, Instruction::MAX_SOURCE_REGISTERS> operands,
      struct simeng::arch::aarch64::InstructionMetadata metadata,
      bool signExtend, bool zeroDestReg) {
    uint8_t r = metadata.operands[2].imm;
    uint8_t s = metadata.operands[3].imm;
    T dest, source;
    if (!zeroDestReg) {
      dest = operands[0].get<T>();
      source = operands[1].get<T>();
    } else {
      dest = 0;
      source = operands[0].get<T>();
    }
    return ExecHelpFunc::bitfieldManipulate(source, dest, r, s, signExtend);
  }
};
}  // namespace aarch64
}  // namespace arch
}  // namespace simeng