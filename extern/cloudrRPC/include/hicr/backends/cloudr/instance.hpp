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
 * @file instance.hpp
 * @brief Provides a definition for the instance class for the CloudR backend
 * @author S. M. Martin
 * @date 18/06/2025
 */
#pragma once

#include <hicr/core/instance.hpp>
#include <hicr/core/topology.hpp>

namespace HiCR::backend::cloudr
{

/**
 * This class represents an abstract definition for a HICR instance as represented by the Cloudr backend:
 */
class Instance final : public HiCR::Instance
{
  public:

  /**
   * Constructor for a Instance class for the CloudR backend
   * \param[in] instanceId The instance identifier corresponding to this HiCR instance
   * \param[in] baseInstance The base instance corresponding to this HiCR instance
   * \param[in] isRoot whether the instance is root
   */
  Instance(const instanceId_t instanceId, HiCR::Instance *const baseInstance, const bool isRoot)
    : HiCR::Instance(instanceId),
      _baseInstance(baseInstance),
      _isRoot(isRoot)
  {}

  /**
   * Default destructor
   */
  ~Instance() override = default;

  /**
   * \return whether the current instance is root
  */
  [[nodiscard]] __INLINE__ bool isRootInstance() const override { return _isRoot; };

  /**
   * Set the instance topology
   * 
   * \param[in] topology
  */
  __INLINE__ void setTopology(const HiCR::Topology &topology) { _topology = topology; }

  /**
   * Topology getter
   * 
   * \return instance topology
  */
  __INLINE__ HiCR::Topology getTopology() const { return _topology; }

  /**
  * Checks whether this instance satisfied a certain instance type.
  * That is, whether it contains the requested devices in the instance type provided
  *
  * The devices are checked in order. That is the first instance device that satisfies a requested device
  * will be removed from the list when checking the next requested device.
  * 
  * @param[in] requestedTopology The topology to check for
  * 
  * @return true, if this instance satisfies the instance type; false, otherwise.
  */
  [[nodiscard]] __INLINE__ bool isCompatible(const HiCR::Topology requestedTopology) { return _topology.isSubset(_topology, requestedTopology); }

  /**
   * Getter for base instance, not the emulated one
   * 
   * \return pointer to instance
  */
  __INLINE__ HiCR::Instance *getBaseInstance() const { return _baseInstance; }

  private:

  /// Emulated topology for this instance
  HiCR::Topology _topology;

  /// Underlying instance implementing this instance
  HiCR::Instance *const _baseInstance;

  /// A flag that determines whether this instance is root
  const bool _isRoot;

  /// A flag that indicates this instance is currently deployed
  bool _isDeployed = false;
};

} // namespace HiCR::backend::cloudr
