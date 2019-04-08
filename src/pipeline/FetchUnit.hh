#pragma once

#include "../Architecture.hh"
#include "../PipelineBuffer.hh"

namespace simeng {
namespace pipeline {

/** A fetch and pre-decode unit for a pipelined processor. Responsible for
 * reading instruction memory and maintaining the program counter. */
class FetchUnit {
 public:
  /** Construct a fetch unit with a reference to an output buffer, the ISA, and
   * the current branch predictor, and information on the instruction memory. */
  FetchUnit(PipelineBuffer<MacroOp>& output, const char* insnPtr,
            unsigned int programByteLength, uint64_t entryPoint,
            const Architecture& isa, BranchPredictor& branchPredictor);

  /** Tick the fetch unit. Retrieves and pre-decodes the instruction at the
   * current program counter. */
  void tick();

  /** Check whether the program has ended. Returns `true` if the current PC is
   * outside of instruction memory. */
  bool hasHalted() const;

  /** Update the program counter to the specified address. */
  void updatePC(uint64_t address);

  /** Retrieve the number of cycles fetch terminated early due to a predicted
   * branch. */
  uint64_t getBranchStalls() const;

 private:
  /** An output buffer connecting this unit to the decode unit. */
  PipelineBuffer<MacroOp>& output_;

  /** The current program counter. */
  uint64_t pc_ = 0;

  /** Pointer to the start of instruction memory. */
  const char* insnPtr_;
  /** The length of the available instruction memory. */
  unsigned int programByteLength_;

  /** Reference to the currently used ISA. */
  const Architecture& isa_;

  /** Reference to the current branch predictor. */
  BranchPredictor& branchPredictor_;

  /** The current program halt state. Set to `true` when the PC leaves the
   * instruction memory region, and set back to `false` if the PC is returned to
   * the instruction region. */
  bool hasHalted_ = false;

  /** The number of cycles fetch terminated early due to a predicted branch. */
  uint64_t branchStalls_ = 0;
};

}  // namespace pipeline
}  // namespace simeng