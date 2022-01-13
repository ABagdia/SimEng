#pragma once

#include <array>
#include <unordered_map>

#include "simeng/BranchPredictor.hh"
#include "simeng/Instruction.hh"
#include "simeng/arch/aarch64/InstructionGroups.hh"

struct cs_arm64_op;

namespace simeng {
namespace arch {
namespace aarch64 {

/** Apply the shift specified by `shiftType` to the unsigned integer `value`,
 * shifting by `amount`. */
template <typename T>
std::enable_if_t<std::is_integral_v<T> && std::is_unsigned_v<T>, T> shiftValue(
    T value, uint8_t shiftType, uint8_t amount) {
  switch (shiftType) {
    case ARM64_SFT_LSL:
      return value << amount;
    case ARM64_SFT_LSR:
      return value >> amount;
    case ARM64_SFT_ASR:
      return static_cast<std::make_signed_t<T>>(value) >> amount;
    case ARM64_SFT_ROR: {
      // Assuming sizeof(T) is a power of 2.
      const auto mask = sizeof(T) * 8 - 1;
      assert((amount <= mask) && "Rotate amount exceeds type width");
      amount &= mask;
      return (value >> amount) | (value << ((-amount) & mask));
    }
    case ARM64_SFT_INVALID:
      return value;
    default:
      assert(false && "Unknown shift type");
      return 0;
  }
}

class Architecture;
struct InstructionMetadata;

namespace RegisterType {
/** The 64-bit general purpose register set: [w|x]0-31. */
const uint8_t GENERAL = 0;
/** The 128|2048 bit vector register set: [v|z]0-31. */
const uint8_t VECTOR = 1;
/** The 32 bit predicate register set: p0-15. */
const uint8_t PREDICATE = 2;
/** The 4-bit NZCV condition flag register. */
const uint8_t NZCV = 3;
/** The system registers. */
const uint8_t SYSTEM = 4;
}  // namespace RegisterType

/** A struct holding user-defined execution information for a aarch64
 * instruction. */
struct ExecutionInfo {
  /** The latency for the instruction. */
  uint16_t latency = 1;

  /** The execution throughput for the instruction. */
  uint16_t stallCycles = 1;

  /** The ports that support the instruction. */
  std::vector<uint8_t> ports = {};
};

enum class InstructionException {
  None = 0,
  EncodingUnallocated,
  EncodingNotYetImplemented,
  ExecutionNotYetImplemented,
  MisalignedPC,
  DataAbort,
  SupervisorCall,
  HypervisorCall,
  SecureMonitorCall,
  NoAvailablePort
};

/** The opcodes of simeng aarch64 micro-operations. */
namespace MicroOpcode {
const uint8_t OFFSET_GEN = 0;
const uint8_t LDR = 1;
const uint8_t STR = 2;
const uint8_t STR_DATA = 3;
}  // namespace MicroOpcode

/** A struct to group micro-operation information together. */
struct MicroOpInfo {
  bool isMicroOp = false;
  bool isLastMicroOp = true;
  int microOpIndex = 0;
};

/** A basic ARMv8-a implementation of the `Instruction` interface. */
class Instruction : public simeng::Instruction {
 public:
  /** Construct an instruction instance by decoding a provided instruction word.
   */
  Instruction(const Architecture& architecture,
              const InstructionMetadata& metadata,
              MicroOpInfo microOpInfo = MicroOpInfo());

  /** Construct an instruction instance that raises an exception. */
  Instruction(const Architecture& architecture,
              const InstructionMetadata& metadata,
              InstructionException exception);

  /** Retrieve the identifier for the first exception that occurred during
   * processing this instruction. */
  virtual InstructionException getException() const;

  /** Retrieve the source registers this instruction reads. */
  const span<Register> getOperandRegisters() const override;

  /** Retrieve the destination registers this instruction will write to.
   * A register value of -1 signifies a Zero Register read, and should not be
   * renamed. */
  const span<Register> getDestinationRegisters() const override;

  /** Check whether the operand at index `i` has had a value supplied. */
  bool isOperandReady(int index) const override;

  /** Override the specified source register with a renamed physical register.
   */
  void renameSource(uint8_t i, Register renamed) override;

  /** Override the specified destination register with a renamed physical
   * register. */
  void renameDestination(uint8_t i, Register renamed) override;

  /** Provide a value for the operand at the specified index. */
  virtual void supplyOperand(uint8_t i, const RegisterValue& value) override;

  /** Check whether all operand values have been supplied, and the instruction
   * is ready to execute. */
  bool canExecute() const override;

  /** Execute the instruction. */
  void execute() override;

  /** Retrieve register results. */
  const span<RegisterValue> getResults() const override;

  /** Generate memory addresses this instruction wishes to access. */
  span<const MemoryAccessTarget> generateAddresses() override;

  /** Retrieve previously generated memory addresses. */
  span<const MemoryAccessTarget> getGeneratedAddresses() const override;

  /** Provide data from a requested memory address. */
  void supplyData(uint64_t address, const RegisterValue& data) override;

  /** Retrieve supplied memory data. */
  span<const RegisterValue> getData() const override;

  /** Early misprediction check; see if it's possible to determine whether the
   * next instruction address was mispredicted without executing the
   * instruction. */
  std::tuple<bool, uint64_t> checkEarlyBranchMisprediction() const override;

  /** Is this a store address operation (a subcategory of store operations which
   * deal with the generation of store addresses to store data at)? */
  bool isStoreAddress() const override;

  /** Is this a store data operation (a subcategory of store operations which
   * deal with the supply of data to be stored)? */
  bool isStoreData() const override;

  /** Is this a load operation? */
  bool isLoad() const override;

  /** Is this a branch operation? */
  bool isBranch() const override;

  /** Is this a return instruction? */
  bool isRET() const override;

  /** Is this a branch and link instruction? */
  bool isBL() const override;

  /** Is this a SVE instruction? */
  bool isSVE() const override;

  /** Retrieve the instruction group this instruction belongs to. */
  uint16_t getGroup() const override;

  /** Set this instruction's execution information including it's execution
   * latency and throughput, and the set of ports which support it. */
  void setExecutionInfo(const ExecutionInfo& info);

  /** Get this instruction's supported set of ports. */
  const std::vector<uint8_t>& getSupportedPorts() override;

  /** Retrieve the instruction's metadata. */
  const InstructionMetadata& getMetadata() const;

  /** A special register value representing the zero register. If passed to
   * `setSourceRegisters`/`setDestinationRegisters`, the value will be
   * automatically supplied as zero. */
  static const Register ZERO_REGISTER;

 private:
  /** The maximum number of source registers any supported AArch64 instruction
   * can have. */
  static const uint8_t MAX_SOURCE_REGISTERS = 6;
  /** The maximum number of destination registers any supported AArch64
   * instruction can have. */
  static const uint8_t MAX_DESTINATION_REGISTERS = 3;

  /** A reference to the ISA instance this instruction belongs to. */
  const Architecture& architecture_;

  /** A reference to the decoding metadata for this instruction. */
  const InstructionMetadata& metadata;

  /** An array of source registers. */
  std::array<Register, MAX_SOURCE_REGISTERS> sourceRegisters;
  /** The number of source registers this instruction reads from. */
  uint8_t sourceRegisterCount = 0;

  /** An array of destination registers. */
  std::array<Register, MAX_DESTINATION_REGISTERS> destinationRegisters;
  /** The number of destination registers this instruction writes to. */
  uint8_t destinationRegisterCount = 0;

  /** An array of provided operand values. Each entry corresponds to a
   * `sourceRegisters` entry. */
  std::array<RegisterValue, MAX_SOURCE_REGISTERS> operands;

  /** An array of generated output results. Each entry corresponds to a
   * `destinationRegisters` entry. */
  std::array<RegisterValue, MAX_DESTINATION_REGISTERS> results;

  /** The current exception state of this instruction. */
  InstructionException exception_ = InstructionException::None;

  // Decoding
  /** Process the instruction's metadata to determine source/destination
   * registers. */
  void decode();

  /** Helper function to check if the current source register is a
   * Zero-register. If it is then it is immediatly decoded as such and added to
   * the instruction's operands list. Otherwise, the count for operands pending
   * is incremented by 1.*/
  void checkZeroReg();

  /** Generate an EncodingNotYetImplemented exception. */
  void nyi();

  /** Generate an EncodingUnallocated exception. */
  void unallocated();

  /** Set the source registers of the instruction, and create a corresponding
   * operands vector. Zero register references will be pre-supplied with a value
   * of 0. */
  void setSourceRegisters(const std::vector<Register>& registers);

  /** Set the destination registers for the instruction, and create a
   * corresponding results vector. */
  void setDestinationRegisters(const std::vector<Register>& registers);

  // Scheduling
  /** The number of operands that have not yet had values supplied. Used to
   * determine execution readiness. */
  short operandsPending = 0;

  // Execution
  /** Generate an ExecutionNotYetImplemented exception. */
  void executionNYI();

  // Instruction Identifiers
  /** Operates on scalar values */
  bool isScalarData_ = false;
  /** Operates on vector values. */
  bool isVectorData_ = false;
  /** Uses Z registers as source and/or destination operands. */
  bool isSVEData_ = false;
  /** Doesn't have a shift operand. */
  bool isNoShift_ = true;
  /** Is a logical operation. */
  bool isLogical_ = false;
  /** Is a compare operation. */
  bool isCompare_ = false;
  /** Is a convert operation. */
  bool isConvert_ = false;
  /** Is a multiply operation. */
  bool isMultiply_ = false;
  /** Is a divide or square root operation */
  bool isDivideOrSqrt_ = false;
  /** Writes to a predicate register */
  bool isPredicate_ = false;
  /** Is a load operation. */
  bool isLoad_ = false;
  /** Is a store address operation. */
  bool isStoreAddress_ = false;
  /** Is a store data operation. */
  bool isStoreData_ = false;
  /** Is a branch operation. */
  bool isBranch_ = false;
  /** Is a return instruction. */
  bool isRET_ = false;
  /** Is a branch and link instructions. */
  bool isBL_ = false;

  // Memory
  /** Set the accessed memory addresses, and create a corresponding memory data
   * vector. */
  void setMemoryAddresses(const std::vector<MemoryAccessTarget>& addresses);

  void setMemoryAddresses(std::vector<MemoryAccessTarget>&& addresses);

  /** The memory addresses this instruction accesses, as a vector of {offset,
   * width} pairs. */
  std::vector<MemoryAccessTarget> memoryAddresses;

  /** A vector of memory values, that were either loaded memory, or are prepared
   * for sending to memory (according to instruction type). Each entry
   * corresponds to a `memoryAddresses` entry. */
  std::vector<RegisterValue> memoryData;

  // Execution helpers
  /** Extend `value` according to `extendType`, and left-shift the result by
   * `shift` */
  uint64_t extendValue(uint64_t value, uint8_t extendType, uint8_t shift) const;

  /** Extend `value` using extension/shifting rules defined in `op`. */
  uint64_t extendOffset(uint64_t value, const cs_arm64_op& op) const;
};

}  // namespace aarch64
}  // namespace arch
}  // namespace simeng
