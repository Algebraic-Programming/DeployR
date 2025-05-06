#include <hicr/backends/pthreads/communicationManager.hpp>
#include <hicr/backends/hwloc/instanceManager.hpp>
#include <hicr/backends/hwloc/memoryManager.hpp>
#include "../engine.hpp"

namespace deployr::engine
{

class Local final : public deployr::Engine
{
  public:

  Local() : deployr::Engine()
  {

  }

  ~Local() = default;

  __INLINE__ void initialize(int* pargc, char*** pargv) override
  {
    _instanceManager = HiCR::backend::hwloc::InstanceManager::createDefault(pargc, pargv);
    _communicationManager = std::make_unique<HiCR::backend::pthreads::CommunicationManager>();
    _memoryManager = std::make_unique<HiCR::backend::hwloc::MemoryManager>();
  };

  __INLINE__ void finalize() override
  {
    
  }

  private:

};

} // namespace deployr::engine