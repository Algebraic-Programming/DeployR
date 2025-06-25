#pragma once

#include <hicr/backends/pthreads/communicationManager.hpp>
#include <hicr/backends/hwloc/instanceManager.hpp>
#include <hicr/backends/hwloc/memoryManager.hpp>
#include "../engine.hpp"

namespace deployr::engine
{

/**
 * Represents a single host environment with no distributed computing capabilities
 */
class Local final : public deployr::Engine
{
  public:

  Local()
    : deployr::Engine()
  {}

  ~Local() = default;

  __INLINE__ void initialize(int *pargc, char ***pargv, std::function<void()> deploymentFc) override
  {
    // Reserving memory for hwloc
    hwloc_topology_init(&_topology);

    _instanceManager      = HiCR::backend::hwloc::InstanceManager::createDefault(pargc, pargv);
    _communicationManager = std::make_unique<HiCR::backend::pthreads::CommunicationManager>();
    _memoryManager        = std::make_unique<HiCR::backend::hwloc::MemoryManager>(&_topology);
  };

  __INLINE__ void finalize() override
  {
    // Freeing up memory
    hwloc_topology_destroy(_topology);

    _instanceManager->finalize();
  }

  __INLINE__ void abort() override { std::abort(); }

  private:

  hwloc_topology_t _topology;
};

} // namespace deployr::engine