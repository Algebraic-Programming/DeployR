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

  __INLINE__ void initialize(int *pargc, char ***pargv, std::function<void()> deploymentFc) override
  {
    _mpiInstanceManager   = std::move(HiCR::backend::mpi::InstanceManager::createDefault(pargc, pargv));
    _instanceManager      = _mpiInstanceManager.get();
    _communicationManager = new HiCR::backend::mpi::CommunicationManager(MPI_COMM_WORLD);
    _memoryManager        = new HiCR::backend::mpi::MemoryManager;

    // Now running deployment function
    deploymentFc();
  };

  __INLINE__ void finalize() override { _instanceManager->finalize(); }

  __INLINE__ void abort() override { MPI_Abort(MPI_COMM_WORLD, 1); }

  private:

  std::unique_ptr<HiCR::InstanceManager> _mpiInstanceManager;
};

} // namespace deployr::engine