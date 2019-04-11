#pragma once

#include <queue>

#include "../RegisterFileSet.hh"

namespace simeng {
namespace pipeline {

using RegisterMappingTable = std::vector<std::vector<uint16_t>>;

/** A Register Alias Table (RAT) implementation. Contains information on
 * the current register renaming state. */
class RegisterAliasTable {
 public:
  /** Construct a RAT, supplying a description of the architectural register
   * structure, the corresponding numbers of physical registers that should
   * be available, and the number of threads to support. */
  RegisterAliasTable(std::vector<RegisterFileStructure> architecturalStructure,
                     std::vector<uint16_t> physicalStructure, uint8_t threads);

  /** Retrieve the current physical register assigned to the provided
   * architectural register. */
  Register getMapping(Register architectural, uint8_t threadId) const;

  /** Determine whether it's possible to allocate `quantity` physical registers
   * of type `type` this cycle. */
  bool canAllocate(uint8_t type, unsigned int quantity) const;

  /** Allocate a physical register for the provided architectural register. */
  Register allocate(Register architectural, uint8_t threadId);

  /** Get the number of free registers available for allocation this cycle. */
  unsigned int freeRegistersAvailable(uint8_t type) const;

  /** Commit the provided physical register. This register now holds the
   * committed state of the corresponding architectural register, and previous
   * physical register is freed. */
  void commit(Register physical, uint8_t threadId);

  /** Rewind the allocation of a physical register. The former physical register
   * is reinstated to the mapping table, and the provided register is freed. */
  void rewind(Register physical, uint8_t threadId);

  /** Free the provided physical register. */
  void free(Register physical);

 private:
  /** The register mapping tables. Holds a map of architectural -> physical
   * register mappings for each register type. */
  std::vector<RegisterMappingTable> mappingTables_;

  /** The register history tables. Each table holds an entry for each physical
   * register, recording the physical register formerly assigned to its
   * architectural register; one table is available per register type. */
  RegisterMappingTable historyTable_;

  /** The register destination tables. Holds a map of physical -> architectural
   * register mappings for each register type. Used for rewind behaviour. */
  RegisterMappingTable destinationTable_;

  /** The free register queues. Holds a list of unallocated physical registers
   * for each register type. */
  std::vector<std::queue<uint16_t>> freeQueues_;
};

}  // namespace pipeline
}  // namespace simeng
