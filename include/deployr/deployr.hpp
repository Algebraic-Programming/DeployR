
#pragma once

#include <hicr/core/exceptions.hpp>
#include <hicr/core/definitions.hpp>
#include <nlohmann_json/json.hpp>
#include <nlohmann_json/parser.hpp>
#include "engine.hpp"
#include "deployment.hpp"

#ifdef _DEPLOYR_DISTRIBUTED_ENGINE_MPI
  #include "engines/mpi.hpp"
#endif

#ifdef _DEPLOYR_DISTRIBUTED_ENGINE_LOCAL
  #include "engines/local.hpp"
#endif

namespace deployr
{

class DeployR final
{
 public:

  DeployR()
  {
    // Instantiating distributed execution engine
    #ifdef _DEPLOYR_DISTRIBUTED_ENGINE_MPI
    _engine = std::make_unique<engine::MPI>();
    #endif

    #ifdef _DEPLOYR_DISTRIBUTED_ENGINE_LOCAL
    _engine = std::make_unique<engine::Local>();
    #endif
  }

  ~DeployR() = default;

  __INLINE__ void initialize(int* pargc, char*** pargv)
  {
    // Initializing distributed execution engine
    _engine->initialize(pargc, pargv);
  }

  __INLINE__ void deploy(Deployment& deployment)
  {
    printf("Deploying '%s'\n", deployment.getName().c_str());
    
    const auto& requests = deployment.getRequests();
    printf("Requests: \n");
    for (const auto& request : requests)
    {
      printf(" + '%s'\n", request.getName().c_str());
      printf("   Replicas: %lu\n", request.getReplicas());
      printf("   Min Host Memory: %lu GB\n", request.getMinHostMemoryGB());
      printf("   Min Host Processing Units: %lu\n", request.getMinHostProcessingUnits());
      printf("   Devices: \n");

      const auto& devices = request.getDevices();
      for (const auto& device : devices)
      {
        printf("    + Type: '%s'\n", device.getType().c_str());
        printf("      Count: %lu\n", device.getCount());
      }
    }
  }

  __INLINE__ void finalize() { _engine->finalize(); }
  __INLINE__ void abort() { _engine->abort(); }
  __INLINE__ bool isRootInstance() const { return _engine->isRootInstance(); }

  private:

  std::unique_ptr<Engine> _engine;

}; // class DeployR

} // namespace deployr