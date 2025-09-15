/*
 *   Copyright 2025 Huawei Technologies Co., Ltd.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 * @file communicationManager.hpp
 * @brief This file implements the communication manager class for the CloudR backend
 * @author S. M. Martin & L. Terracciano
 * @date 19/12/2023
 */

#pragma once

#include <hicr/core/definitions.hpp>
#include <hicr/core/localMemorySlot.hpp>
#include <hicr/core/communicationManager.hpp>
#include <hicr/backends/mpi/communicationManager.hpp>
#include <hicr/backends/cloudr/instanceManager.hpp>
#include "instanceManager.hpp"

namespace HiCR::backend::cloudr
{

/**
 * Implementation of the CloudR backend
 */
class CommunicationManager final : public HiCR::CommunicationManager
{
  public:

  /**
   * Constructor for the cloudr backend.
   *
   * \param[in] communicationManager working communication manager to implement the required functionalities (e.g, MPI instance manager)
   * \param[in] instanceManager working instance manager to handle the virtualized resources and trigger RPC for all the instances (free and non) 
   */
  CommunicationManager(HiCR::CommunicationManager *communicationManager, HiCR::backend::cloudr::InstanceManager *instanceManager)
    : HiCR::CommunicationManager(),
      _communicationManager(communicationManager),
      _instanceManager(instanceManager)
  {}

  ~CommunicationManager() override = default;

  /**
   * Forward call to getGlobalMemorySlotImpl
   *
   * \param[in] tag Tag that identifies a subset of all global memory slots
   * \param[in] globalKey The sorting key inside the tag subset that distinguished between registered slots
   * \return The map of registered global memory slots, filtered by tag and mapped by key
   */
  std::shared_ptr<GlobalMemorySlot> getGlobalMemorySlotImpl(GlobalMemorySlot::tag_t tag, GlobalMemorySlot::globalKey_t globalKey) override
  {
    // Forward call to base communication manager
    return _communicationManager->getGlobalMemorySlot(tag, globalKey);
  }

  /**
   * Forward call to exchangeGlobalMemorySlots
   *
   * \param[in] tag Tag that identifies a subset of all global memory slots
   * \param[in] memorySlots memory slots to exchange
   */
  __INLINE__ void exchangeGlobalMemorySlotsImpl(HiCR::GlobalMemorySlot::tag_t tag, const std::vector<globalKeyMemorySlotPair_t> &memorySlots) override
  {
    // Request non active instances to participate in the exchange
    if (_instanceManager->getCurrentInstance()->isRootInstance()) _instanceManager->requestExchangeGlobalMemorySlots(tag);

    // Initiate the exchange
    _communicationManager->exchangeGlobalMemorySlots(tag, memorySlots);

    // Keep track of the initiated exchange
    _isExchangePending = true;
  }

  /**
   * Forward call to queryMemorySlotUpdatesImpl
   *
   * \param[in] memorySlot memory slots to query
   */
  void queryMemorySlotUpdatesImpl(std::shared_ptr<LocalMemorySlot> memorySlot) override
  {
    // Forward call to base communication manager
    _communicationManager->queryMemorySlotUpdates(memorySlot);
  }

  /**
   * Forward call to destroyGlobalMemorySlotImpl
   *
   * \param[in] memorySlot memory slots to destroy
   */
  void destroyGlobalMemorySlotImpl(std::shared_ptr<GlobalMemorySlot> memorySlot) override
  {
    // Forward call to base communication manager
    _communicationManager->destroyGlobalMemorySlot(memorySlot);
  }

  /**
   * Forward call to fenceImpl
   *
   * \param[in] tag tag on which fence should be performed
   */
  __INLINE__ void fenceImpl(HiCR::GlobalMemorySlot::tag_t tag) override
  {
    // Request non active instances to participate in the fence
    if (_instanceManager->getCurrentInstance()->isRootInstance()) _instanceManager->requestFence(tag);

    // Execute the fence
    _communicationManager->fence(tag);

    // If any exchange operation was initiated
    if (_isExchangePending == true)
    {
      // Get the global memory slot tag-key map
      const auto &globalMap = _communicationManager->getGlobalMemorySlotTagKeyMap();

      // Update CloudR global memory slot tag-key map
      setGlobalMemorySlotTagKeyMap(globalMap);

      // All the changes of the exchange operations are reflected in CloudR
      _isExchangePending = false;
    }
  }

  /**
   * Forward call to acquireGlobalLockImpl
   *
   * \param[in] memorySlot memory slots to lock
   * 
   * \return bool if the operation was successful
   */
  bool acquireGlobalLockImpl(std::shared_ptr<GlobalMemorySlot> memorySlot) override
  {
    // Forward call to base communication manager
    return _communicationManager->acquireGlobalLock(memorySlot);
  }

  /**
   * Forward call to releaseGlobalLockImpl
   *
   * \param[in] memorySlot memory slots to unlock
   */
  void releaseGlobalLockImpl(std::shared_ptr<GlobalMemorySlot> memorySlot) override
  {
    // Forward call to base communication manager
    return _communicationManager->releaseGlobalLock(memorySlot);
  }

  /**
   * Forward call to serializeGlobalMemorySlot
   *
   * \param[in] globalSlot global memory slot to serialize
   * 
   * \return pointer to the serialized versoin
   */
  uint8_t *serializeGlobalMemorySlot(const std::shared_ptr<HiCR::GlobalMemorySlot> &globalSlot) const override
  {
    // Forward call to base communication manager
    return _communicationManager->serializeGlobalMemorySlot(globalSlot);
  }

  /**
   * Forward call to deserializeGlobalMemorySlot
   *
   * \param[in] buffer the serialize global memory slot
   * \param[in] tag global memory slot tag
   * 
   * \return a deserialized global memory slot
   */
  std::shared_ptr<GlobalMemorySlot> deserializeGlobalMemorySlot(uint8_t *buffer, GlobalMemorySlot::tag_t tag) override
  {
    // Forward call to base communication manager
    return _communicationManager->deserializeGlobalMemorySlot(buffer, tag);
  }

  /**
   * Forward call to promoteLocalMemorySlot
   *
   * \param[in] localMemorySlot memory slot to promote
   * \param[in] tag the tag where it should be promoted
   * 
   * \return global memory slot
   */
  std::shared_ptr<GlobalMemorySlot> promoteLocalMemorySlot(const std::shared_ptr<LocalMemorySlot> &localMemorySlot, GlobalMemorySlot::tag_t tag) override
  {
    // Forward call to base communication manager
    return _communicationManager->promoteLocalMemorySlot(localMemorySlot, tag);
  }

  /**
   * Forward call to destroyPromotedGlobalMemorySlot
   *
   * \param[in] memorySlot memory slots to destroy
   */
  void destroyPromotedGlobalMemorySlot(const std::shared_ptr<GlobalMemorySlot> &memorySlot) override
  {
    // Forward call to base communication manager
    _communicationManager->destroyGlobalMemorySlot(memorySlot);
  }

  /**
   * Forward call to flushReceived
   */
  void flushReceived() override
  {
    // Forward call to base communication manager
    _communicationManager->flushReceived();
  }

  /**
   * Forward call to flushSent
   */
  void flushSent() override
  {
    // Forward call to base communication manager
    _communicationManager->flushSent();
  }

  /**
   * Forward call to deregisterGlobalMemorySlotImpl
   *
   * \param[in] memorySlot memory slots to deregister
   */
  void deregisterGlobalMemorySlotImpl(const std::shared_ptr<GlobalMemorySlot> &memorySlot) override
  {
    // Forward call to base communication manager
    _communicationManager->deregisterGlobalMemorySlot(memorySlot);
  }

  /**
   * Forward call to memcpyImpl
   *
   * \param[in] destination
   * \param[in] dst_offset
   * \param[in] source 
   * \param[in] src_offset
   * \param[in] size 
   */
  void memcpyImpl(const std::shared_ptr<LocalMemorySlot> &destination, size_t dst_offset, const std::shared_ptr<LocalMemorySlot> &source, size_t src_offset, size_t size) override
  {
    // Forward call to base communication manager
    _communicationManager->memcpy(destination, dst_offset, source, src_offset, size);
  }

  /**
   * Forward call to memcpyImpl
   *
   * \param[in] destination
   * \param[in] dst_offset
   * \param[in] source 
   * \param[in] src_offset
   * \param[in] size 
   */
  void memcpyImpl(const std::shared_ptr<GlobalMemorySlot> &destination, size_t dst_offset, const std::shared_ptr<LocalMemorySlot> &source, size_t src_offset, size_t size) override
  {
    // Forward call to base communication manager
    _communicationManager->memcpy(destination, dst_offset, source, src_offset, size);
  }

  /**
   * Forward call to memcpyImpl
   *
   * \param[in] destination
   * \param[in] dst_offset
   * \param[in] source 
   * \param[in] src_offset
   * \param[in] size 
   */
  void memcpyImpl(const std::shared_ptr<LocalMemorySlot> &destination, size_t dst_offset, const std::shared_ptr<GlobalMemorySlot> &source, size_t src_offset, size_t size) override
  {
    // Forward call to base communication manager
    _communicationManager->memcpy(destination, dst_offset, source, src_offset, size);
  }

  /**
   * Forward call to fenceImpl
   *
   * \param[in] slot
   * \param[in] expectedSent
   * \param[in] expectedRcvd
   */
  void fenceImpl(const std::shared_ptr<LocalMemorySlot> &slot, size_t expectedSent, size_t expectedRcvd) override
  {
    // Forward call to base communication manager
    _communicationManager->fence(slot, expectedSent, expectedRcvd);
  }

  /**
   * Forward call to fenceImpl
   *
   * \param[in] slot
   * \param[in] expectedSent
   * \param[in] expectedRcvd
   */
  void fenceImpl(const std::shared_ptr<GlobalMemorySlot> &slot, size_t expectedSent, size_t expectedRcvd) override
  {
    // Forward call to base communication manager
    _communicationManager->fence(slot, expectedSent, expectedRcvd);
  }

  private:

  /**
   * HiCR Communication manager that does the actual operations
  */
  HiCR::CommunicationManager *const _communicationManager;

  /**
   * HiCR Instance manager to check whether this is a root instance
  */
  HiCR::backend::cloudr::InstanceManager *const _instanceManager;

  /**
   * Keep track if there are pending exchanges operations
  */
  bool _isExchangePending = false;
};

} // namespace HiCR::backend::cloudr
