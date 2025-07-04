#pragma once

#include <hicr/backends/mpi/communicationManager.hpp>
#include <hicr/backends/mpi/instanceManager.hpp>
#include <hicr/backends/mpi/memoryManager.hpp>
#include "../engine.hpp"

namespace deployr::engine
{

/**
 * Represents an MPI-based engine with the capability of deploying multiple instances at launch time (will not support runtime instance creation)
 */
class MPI final : public deployr::Engine
{
  public:

  MPI()
    : deployr::Engine()
  {}

  ~MPI() = default;

  __INLINE__ void initializeImpl(int *pargc, char ***pargv, std::function<void()> deploymentFc) override
  {
    _mpiInstanceManager   = std::move(HiCR::backend::mpi::InstanceManager::createDefault(pargc, pargv));
    _instanceManager      = _mpiInstanceManager.get();
    _communicationManager = new HiCR::backend::mpi::CommunicationManager(MPI_COMM_WORLD);
    _memoryManager        = new HiCR::backend::mpi::MemoryManager;

    // Now running deployment function
    if (_instanceManager->getCurrentInstance()->isRootInstance() == false) deploymentFc();
  };

  __INLINE__ void finalize() override { _instanceManager->finalize(); }

  __INLINE__ void abort() override { MPI_Abort(MPI_COMM_WORLD, 1); }

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

  std::unique_ptr<HiCR::InstanceManager> _mpiInstanceManager;
  std::unique_ptr<HiCR::frontend::RPCEngine> _rpcEnginePtr;


};

} // namespace deployr::engine