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

  __INLINE__ void initializeImpl(int *pargc, char ***pargv, std::function<void()> deploymentFc) override
  {
    // Reserving memory for hwloc
    hwloc_topology_init(&_topology);

    _instanceManager      = HiCR::backend::hwloc::InstanceManager::createDefault(pargc, pargv);
    _communicationManager = std::make_unique<HiCR::backend::pthreads::CommunicationManager>();
    _memoryManager        = std::make_unique<HiCR::backend::hwloc::MemoryManager>(&_topology);

    if (_instanceManager->getCurrentInstance()->isRootInstance() == false) deploymentFc(); 
  };

  __INLINE__ void finalize() override
  {
    // Freeing up memory
    hwloc_topology_destroy(_topology);

    _instanceManager->finalize();
  }

  __INLINE__ void abort() override { std::abort(); }

  __INLINE__ void deploy() override
  {
    // Finding the first memory space and compute resource to create our RPC engine
    auto RPCMemorySpace     = _firstDevice->getMemorySpaceList().begin().operator*();
    auto RPCComputeResource = _firstDevice->getComputeResourceList().begin().operator*();

    // Instantiating RPC engine
    _rpcEnginePtr = std::make_unique<HiCR::frontend::RPCEngine>(*_communicationManager, *_instanceManager, *_memoryManager, *_computeManager, RPCMemorySpace, RPCComputeResource);
    _rpcEngine = _rpcEnginePtr.get();

    // Initializing RPC engine
    _rpcEngine->initialize();    
  }

  private:

  hwloc_topology_t _topology;
  std::unique_ptr<HiCR::frontend::RPCEngine> _rpcEnginePtr;
};

} // namespace deployr::engine