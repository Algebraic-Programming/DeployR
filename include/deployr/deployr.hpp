
#pragma once

#include <hicr/core/exceptions.hpp>
#include <hicr/core/definitions.hpp>
#include <nlohmann_json/json.hpp>
#include <nlohmann_json/parser.hpp>
#include "engine.hpp"
#include "request.hpp"
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

    // If this is not the root instance, wait for incoming RPCs
    if (_engine->isRootInstance() == false) while(true);
  }

  __INLINE__ Deployment deploy(Request& request)
  {
    // Printing information before deploying
    printRequestInfo(request);

    // Counting the exact number of instances requested.
    size_t instancesRequested = 0;
    for (const auto& request : request.getMachines()) instancesRequested += request.getReplicas();

    // Getting the initial number of instances
    size_t initialInstanceCount = _engine->getInstanceCount();

    // Printing instance count information
    printf("[DeployR] Initial Instances:   %lu\n", initialInstanceCount);
    printf("[DeployR] Requested Instances: %lu\n", instancesRequested);

    // We handle the following scenarios:
    // With N: number of requested instances
    // With K: number of initial instances
    
    // If K > N, more initial instances than requested provided. Abort execution.
    if (initialInstanceCount > instancesRequested)
    {
      fprintf(stderr, "[DeployR] More initial instances (%lu) provided than required (%lu) were provided.\n", initialInstanceCount, instancesRequested);
      _engine->abort();
    } 

    // If 1 < K < N, this is the hybrid scenario, not handled as it is complex and unlikely to be required. 
    if (initialInstanceCount > 1 && initialInstanceCount < instancesRequested)
    {
      fprintf(stderr, "[DeployR] Irregular number of initial instances (%lu) provided. Must be either 1 or %lu for this request.\n", initialInstanceCount, instancesRequested);
      _engine->abort();
    } 

    // If K == 1, this is the cloud scenario. N-1 instances will be created.
    if (initialInstanceCount == 1 && initialInstanceCount < instancesRequested)
    {
      fprintf(stderr, "[DeployR] TBD: create more instances according with requested instance hardware requirements\n");
      _engine->abort();
    } 

    // Proceed with request to instance matching
    
 
     Deployment deployment;
     return deployment;
  }

  __INLINE__ void printRequestInfo(const Request& request)
  {
    printf("[DeployR] Request: '%s'\n", request.getName().c_str());
    
    const auto& machines = request.getMachines();
    printf("[DeployR]   Machines: \n");
    for (const auto& machine : machines)
    {
      printf("[DeployR]  + '%s'\n", machine.getName().c_str());
      printf("[DeployR]    Replicas: %lu\n", machine.getReplicas());
      printf("[DeployR]    Min Host Memory: %lu GB\n", machine.getMinHostMemoryGB());
      printf("[DeployR]    Min Host Processing Units: %lu\n", machine.getMinHostProcessingUnits());
      printf("[DeployR]    Devices: \n");

      const auto& devices = machine.getDevices();
      for (const auto& device : devices)
      {
        printf("[DeployR]      + Type: '%s'\n", device.getType().c_str());
        printf("[DeployR]        Count: %lu\n", device.getCount());
      }
    }
  }

  __INLINE__ size_t getInstanceCount() const { return _engine->getInstanceCount(); }
  __INLINE__ void finalize() { _engine->finalize(); }
  __INLINE__ void abort() { _engine->abort(); }
  __INLINE__ bool isRootInstance() const { return _engine->isRootInstance(); }

  private:

  std::unique_ptr<Engine> _engine;

}; // class DeployR

} // namespace deployr