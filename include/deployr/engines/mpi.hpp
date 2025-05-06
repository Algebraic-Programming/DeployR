#include <hicr/backends/mpi/communicationManager.hpp>
#include <hicr/backends/mpi/instanceManager.hpp>
#include <hicr/backends/mpi/memoryManager.hpp>
#include "../engine.hpp"

namespace deployr::engine
{

class MPI final : public deployr::Engine
{
  public:

  MPI() : deployr::Engine()
  {
    
  }

  ~MPI() = default;

  __INLINE__ void initialize(int* pargc, char*** pargv) override
  {
    _instanceManager = HiCR::backend::mpi::InstanceManager::createDefault(pargc, pargv);
    _communicationManager = std::make_unique<HiCR::backend::mpi::CommunicationManager>(MPI_COMM_WORLD);
    _memoryManager = std::make_unique<HiCR::backend::mpi::MemoryManager>();
  };

  __INLINE__ void finalize() override
  {
    
  }
  
  private:

};

} // namespace deployr::engine