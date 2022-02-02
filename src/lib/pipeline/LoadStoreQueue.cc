#include "simeng/pipeline/LoadStoreQueue.hh"

#include <array>
#include <cassert>
#include <cstring>
#include <iostream>

namespace simeng {
namespace pipeline {

/** Check whether requests `a` and `b` overlap. */
bool requestsOverlap(MemoryAccessTarget a, MemoryAccessTarget b) {
  // Check whether one region ends before the other begins, implying no overlap,
  // and negate
  return !(a.address + a.size <= b.address || b.address + b.size <= a.address);
}

LoadStoreQueue::LoadStoreQueue(
    unsigned int maxCombinedSpace, MemoryInterface& memory,
    span<PipelineBuffer<std::shared_ptr<Instruction>>> completionSlots,
    std::function<void(span<Register>, span<RegisterValue>)> forwardOperands,
    uint8_t L1Bandwidth, uint8_t permittedRequests, uint8_t permittedLoads,
    uint8_t permittedStores)
    : completionSlots_(completionSlots),
      forwardOperands_(forwardOperands),
      maxCombinedSpace_(maxCombinedSpace),
      combined_(true),
      memory_(memory),
      L1Bandwidth_(L1Bandwidth),
      totalLimit_(permittedRequests),
      // Set per-cycle limits for each request type
      reqLimits_{permittedLoads, permittedStores} {};

LoadStoreQueue::LoadStoreQueue(
    unsigned int maxLoadQueueSpace, unsigned int maxStoreQueueSpace,
    MemoryInterface& memory,
    span<PipelineBuffer<std::shared_ptr<Instruction>>> completionSlots,
    std::function<void(span<Register>, span<RegisterValue>)> forwardOperands,
    uint8_t L1Bandwidth, uint8_t permittedRequests, uint8_t permittedLoads,
    uint8_t permittedStores)
    : completionSlots_(completionSlots),
      forwardOperands_(forwardOperands),
      maxLoadQueueSpace_(maxLoadQueueSpace),
      maxStoreQueueSpace_(maxStoreQueueSpace),
      combined_(false),
      memory_(memory),
      L1Bandwidth_(L1Bandwidth),
      totalLimit_(permittedRequests),
      // Set per-cycle limits for each request type
      reqLimits_{permittedLoads, permittedStores} {};

unsigned int LoadStoreQueue::getLoadQueueSpace() const {
  if (combined_) {
    return getCombinedSpace();
  } else {
    return getLoadQueueSplitSpace();
  }
}
unsigned int LoadStoreQueue::getStoreQueueSpace() const {
  if (combined_) {
    return getCombinedSpace();
  } else {
    return getStoreQueueSplitSpace();
  }
}
unsigned int LoadStoreQueue::getTotalSpace() const {
  if (combined_) {
    return getCombinedSpace();
  } else {
    return getLoadQueueSplitSpace() + getStoreQueueSplitSpace();
  }
}

unsigned int LoadStoreQueue::getLoadQueueSplitSpace() const {
  return maxLoadQueueSpace_ - loadQueue_.size();
}
unsigned int LoadStoreQueue::getStoreQueueSplitSpace() const {
  return maxStoreQueueSpace_ - storeQueue_.size();
}
unsigned int LoadStoreQueue::getCombinedSpace() const {
  return maxCombinedSpace_ - loadQueue_.size() - storeQueue_.size();
}

void LoadStoreQueue::addLoad(const std::shared_ptr<Instruction>& insn) {
  loadQueue_.push_back(insn);
}
void LoadStoreQueue::addStore(const std::shared_ptr<Instruction>& insn) {
  storeQueue_.push_back({insn, {}});
}

void LoadStoreQueue::startLoad(const std::shared_ptr<Instruction>& insn) {
  const auto& addresses = insn->getGeneratedAddresses();
  // std::cout << "startLoad: " << insn->getSequenceId() << ":"
  //           << insn->getInstructionId() << ":0x" << std::hex
  //           << insn->getInstructionAddress() << std::dec << ":"
  //           << insn->getMicroOpIndex() << ":" << std::endl;
  if (addresses.size() == 0) {
    insn->execute();
    completedLoads_.push(insn);
  } else {
    requestQueue_.push_back({tickCounter_ + insn->getLSQLatency(), {}, insn});
    for (size_t i = 0; i < addresses.size(); i++) {
      requestQueue_.back().reqAddresses.push(addresses[i]);
      // std::cout << "\t0x" << std::hex << addresses[i].address << std::dec
      //           << std::endl;
    }
    requestedLoads_.emplace(insn->getSequenceId(), insn);
  }
}

void LoadStoreQueue::supplyStoreData(const std::shared_ptr<Instruction>& insn) {
  if (!insn->isStoreData()) return;
  // Get identifier values
  const uint64_t macroOpNum = insn->getInstructionId();
  const int microOpNum = insn->getMicroOpIndex();

  // Get data
  span<const simeng::RegisterValue> data = insn->getData();

  // Find storeQueue_ entry which is linked to the store data operation
  auto itSt = storeQueue_.begin();
  while (itSt != storeQueue_.end()) {
    auto& entry = itSt->first;
    // Pair entry and incoming store data operation with macroOp identifier and
    // microOp index value pre-detemined in microDecoder
    if (entry->getInstructionId() == macroOpNum &&
        entry->getMicroOpIndex() == microOpNum) {
      // Supply data to be stored by operations
      itSt->second = data;
      // std::cout << "\tsupplyStoreData: " << itSt->first->getSequenceId() <<
      // ":"
      //           << itSt->first->getInstructionId() << ":0x" << std::hex
      //           << itSt->first->getInstructionAddress() << std::dec << ":"
      //           << itSt->first->getMicroOpIndex() << " -> "
      //           << insn->getSequenceId() << ":" << insn->getInstructionId()
      //           << ":0x" << std::hex << insn->getInstructionAddress()
      //           << std::dec << ":" << insn->getMicroOpIndex() << ":"
      //           << std::endl;
      // for (auto d : itSt->second) {
      //   if (d.size() == 4)
      //     std::cout << "\t\t" << d.get<uint32_t>() << std::endl;
      //   else if (d.size() == 8)
      //     std::cout << "\t\t" << d.get<uint64_t>() << std::endl;
      // }
      break;
    } else {
      itSt++;
    }
  }
}

bool LoadStoreQueue::commitStore(const std::shared_ptr<Instruction>& uop) {
  assert(storeQueue_.size() > 0 &&
         "Attempted to commit a store from an empty queue");
  assert(storeQueue_.front().first->getSequenceId() == uop->getSequenceId() &&
         "Attempted to commit a store that wasn't present at the front of the "
         "store queue");

  const auto& addresses = uop->getGeneratedAddresses();

  const uint64_t microOpNum = uop->getSequenceId();
  span<const simeng::RegisterValue> data = storeQueue_.front().second;

  requestQueue_.push_back({tickCounter_ + uop->getLSQLatency(), {}, uop});
  // Submit request write to memory interface early as the architectural state
  // considers the store to be retired and thus its operation complete
  for (size_t i = 0; i < addresses.size(); i++) {
    // std::cout << "\tStore: " << uop->getSequenceId() << ":"
    //           << uop->getInstructionId() << ":0x" << std::hex
    //           << uop->getInstructionAddress() << std::dec << ":0x" <<
    //           std::hex
    //           << addresses[i].address << std::dec << ":"
    //           << uop->getMicroOpIndex() << " <- ";
    // if (data[i].size() == 1)
    //   std::cout << unsigned(data[i].get<uint8_t>());
    // else if (data[i].size() == 2)
    //   std::cout << data[i].get<uint16_t>();
    // else if (data[i].size() == 4)
    //   std::cout << data[i].get<uint32_t>();
    // else if (data[i].size() == 8)
    //   std::cout << data[i].get<uint64_t>();
    // else if (data[i].size() == 256)
    //   std::cout << data[i].getAsVector<uint64_t>()[0] << ":"
    //             << data[i].getAsVector<uint64_t>()[1];
    // else
    //   std::cout << "N/A";
    // std::cout << std::endl;
    memory_.requestWrite(addresses[i], data[i]);
    // Still add addresses to requestQueue_ to ensure contention of resources is
    // correctly simulated
    requestQueue_.back().reqAddresses.push(addresses[i]);
  }

  // Check all loads that have requested memory
  violatingLoad_ = nullptr;
  for (const auto& load : requestedLoads_) {
    // Skip loads that are younger than the oldest violating load
    if (violatingLoad_ &&
        load.second->getSequenceId() > violatingLoad_->getSequenceId())
      continue;
    // Violation invalid if the load and store entries are generated by the same
    // uop
    if (load.second->getSequenceId() != uop->getSequenceId()) {
      const auto& loadedAddresses = load.second->getGeneratedAddresses();
      // Iterate over store addresses
      for (const auto& storeReq : addresses) {
        // Iterate over load addresses
        for (const auto& loadReq : loadedAddresses) {
          // Check for overlapping requests, and flush if discovered
          if (requestsOverlap(storeReq, loadReq)) {
            violatingLoad_ = load.second;
            reorderingFlushes_++;
          }
        }
      }
    }
  }

  storeQueue_.pop_front();

  return violatingLoad_ != nullptr;
}

void LoadStoreQueue::commitLoad(const std::shared_ptr<Instruction>& uop) {
  assert(loadQueue_.size() > 0 &&
         "Attempted to commit a load from an empty queue");
  assert(loadQueue_.front()->getSequenceId() == uop->getSequenceId() &&
         "Attempted to commit a load that wasn't present at the front of the "
         "load queue");

  auto it = loadQueue_.begin();
  while (it != loadQueue_.end()) {
    auto& entry = *it;
    if (entry->isLoad()) {
      requestedLoads_.erase(entry->getSequenceId());
      it = loadQueue_.erase(it);
      break;
    } else {
      it++;
    }
  }
}

void LoadStoreQueue::purgeFlushed() {
  auto itLd = loadQueue_.begin();
  while (itLd != loadQueue_.end()) {
    auto& entry = *itLd;
    if (entry->isFlushed()) {
      requestedLoads_.erase(entry->getSequenceId());
      itLd = loadQueue_.erase(itLd);
    } else {
      itLd++;
    }
  }

  auto itSt = storeQueue_.begin();
  while (itSt != storeQueue_.end()) {
    auto& entry = itSt->first;
    if (entry->isFlushed()) {
      // std::cout << "\tErasing from storeQueue_: "
      //           << itSt->first->getSequenceId() << ":"
      //           << itSt->first->getInstructionId() << ":0x" << std::hex
      //           << itSt->first->getInstructionAddress() << std::dec << ":"
      //           << itSt->first->getMicroOpIndex() << ":" << std::endl;
      itSt = storeQueue_.erase(itSt);
    } else {
      itSt++;
    }
  }

  auto it2 = requestQueue_.begin();
  while (it2 != requestQueue_.end()) {
    auto& entry = it2->insn;
    if (entry->isFlushed()) {
      it2 = requestQueue_.erase(it2);
    } else {
      it2++;
    }
  }
}

void LoadStoreQueue::tick() {
  tickCounter_++;
  // Send memory requests adhering to set bandwidth and number of permitted
  // requests per cycle
  uint64_t dataTransfered = 0;
  std::array<uint8_t, 2> reqCounts = {0, 0};
  bool remove = true;
  while (requestQueue_.size() > 0) {
    uint8_t isWrite = 0;
    auto& entry = requestQueue_.front();
    if (entry.readyAt <= tickCounter_) {
      // std::cout << "--------" << std::endl;
      if (!entry.insn->isLoad()) {
        isWrite = 1;
      }
      // Deal with requests from queue of addresses in requestQueue_ entry
      auto& addressQueue = entry.reqAddresses;
      while (addressQueue.size()) {
        const simeng::MemoryAccessTarget req = addressQueue.front();

        // if (reqCounts[1] || (reqCounts[0] && isWrite)) {
        // std::cout << "\tHalting LSQ req, store cannot occur (reads: "
        //           << unsigned(reqCounts[0])
        //           << ", writes: " << unsigned(reqCounts[1]) << ")"
        //           << std::endl;
        //   remove = false;
        //   break;
        // }

        // Ensure the limit on the number of permitted operations is adhered to
        reqCounts[isWrite]++;
        if (reqCounts[isWrite] > reqLimits_[isWrite] ||
            reqCounts[isWrite] + reqCounts[!isWrite] > totalLimit_) {
          // std::cout
          //     << "\tHalting LSQ req, too many requests this cycle (reads: "
          //     << unsigned(reqCounts[0])
          //     << ", writes: " << unsigned(reqCounts[1]) << ")" << std::endl;
          remove = false;
          break;
        }

        // Ensure the limit on the data transfered per cycle is adhered to
        assert(req.size < L1Bandwidth_ &&
               "Individual memory request from LoadStoreQueue exceeds L1 "
               "bandwidth set and thus will never be submitted");
        dataTransfered += req.size;
        if (dataTransfered > L1Bandwidth_) {
          // std::cout << "\tHalting LSQ req, exceeded bandwidth this cycle ("
          //           << dataTransfered << ")" << std::endl;
          remove = false;
          break;
        }
        // Request a read from the memory interface if the requestQueue_ entry
        // represents a read
        // std::cout << "LSQ REQ: 0x" << std::hex
        //           << entry.insn->getInstructionAddress() << std::dec
        //           << " accessing 0x" << std::hex << req.address << std::dec
        //           << std::endl;
        if (!isWrite) {
          memory_.requestRead(req, entry.insn->getSequenceId());
        }
        addressQueue.pop();
      }
      // Only remove entry from requestQueue_ if all addresses in entry are
      // processed
      if (remove)
        requestQueue_.pop_front();
      else
        break;
    } else {
      break;
    }
  }

  // Process completed read requests
  for (const auto& response : memory_.getCompletedReads()) {
    auto address = response.target.address;
    const auto& data = response.data;

    // TODO: Detect and handle non-fatal faults (e.g. page fault)

    // Find instruction that requested the memory read
    auto itr = requestedLoads_.find(response.requestId);
    if (itr == requestedLoads_.end()) {
      continue;
    }

    // Supply data to the instruction and execute if it is ready
    auto load = itr->second;
    load->supplyData(address, data);
    if (load->hasAllData()) {
      // This load has completed
      load->execute();
      if (load->isStoreData()) {
        // std::cout << "\tinto str data handle" << std::endl;
        supplyStoreData(load);
      }
      completedLoads_.push(load);
    }
  }
  memory_.clearCompletedReads();

  // Pop from the front of the completed loads queue and send to writeback
  size_t count = 0;
  while (completedLoads_.size() > 0 && count < completionSlots_.size()) {
    const auto& insn = completedLoads_.front();

    // Forward the results
    // std::cout << "Forwarding operands from load 0x" << std::hex
    //           << insn->getInstructionAddress() << std::dec << std::endl;
    forwardOperands_(insn->getDestinationRegisters(), insn->getResults());

    completionSlots_[count].getTailSlots()[0] = std::move(insn);

    completedLoads_.pop();

    count++;
  }
}

std::shared_ptr<Instruction> LoadStoreQueue::getViolatingLoad() const {
  return violatingLoad_;
}

bool LoadStoreQueue::isCombined() const { return combined_; }

uint64_t LoadStoreQueue::getReorderingFlushes() const {
  return reorderingFlushes_;
}

}  // namespace pipeline
}  // namespace simeng
