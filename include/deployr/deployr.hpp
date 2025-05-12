
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

    // Committing rpcs to the engine
    for (const auto& rpc : _registeredFunctions) _engine->registerRPC(rpc.first, rpc.second);

    // If this is not the root instance, wait for incoming RPCs
    if (_engine->isRootInstance() == false) 
    {
      // Listening for an incoming RPC
      _engine->listenRPCs();

      // Only a single user-defined RPC shall be executed by a non-root instance
      _engine->finalize();

      // Exiting regularly.
      exit(0);
    }
  }

  __INLINE__ void deploy(Request& request)
  {
    // Printing information before deploying
    printRequestInfo(request);

    // Counting the exact number of instances requested.
    size_t instancesRequested = 0;
    for (const auto& machine : request.getMachines()) instancesRequested += machine.getReplicas();

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

    // Getting global system topology
    auto globalTopology = _engine->getGlobalTopology();

    // Printing topology
    //printf("[DeployR] Detected Global Topology\n");
    // for (size_t i = 0; i < globalTopology.size(); i++)
    // {
      // printf("Instance: %lu\n", i);
      //printf("%s\n", globalTopology[i].serialize().dump(2).c_str());
      //printf("--------\n");
    // }

    // Building deployment object
    for (const auto& machine : request.getMachines())
      for (size_t i = 0; i < machine.getReplicas(); i++)
       _deployment.addMachine(machine);
    
    for (const auto& topology : globalTopology)
       _deployment.addResource(topology);

    // Proceed with request to instance matching
    if (_deployment.performMatching() == false)
    {
      fprintf(stderr, "[DeployR] The provided resources are not sufficient for the requested instances.\n");
      _engine->abort();
    } 

    // Getting root instance index
    _rootInstanceIdx = _engine->getRootInstanceIndex();
    printf("Root Instance Idx: %lu\n", _rootInstanceIdx);

    // Getting pairings
    auto pairings = _deployment.getPairings();

    // Launching initial function to each of the requested machines
    size_t currentMachineIdx = 0;
    for (const auto& machine : request.getMachines())
      for (size_t i = 0; i < machine.getReplicas(); i++)
      {
        // Getting the destination resource idx paired to this machine request
        const auto resourceIdx = pairings[currentMachineIdx];

        // Getting the function to run for the paired machine
        const auto fcName = machine.getFunction();
        
        // Checking the requested function was registered
        if (_registeredFunctions.contains(fcName) == false)
        {
            fprintf(stderr, "The requested function name '%s' is not registered. Please register it before initializing DeployR.\n", fcName.c_str());
            abort();
        } 
        
       
        // Launching initial function in the destination instance
        // If the resource index is the root instance, then don't send RPC. It will be executed manually
        if (resourceIdx != _rootInstanceIdx)
        {
          printf("Launching RPC: ResourceIdx %lu, FunctionName: %s\n", resourceIdx, fcName.c_str());
          launchFunction(resourceIdx, machine.getFunction());
        } 
        else
        {
          printf("Running Root: ResourceIdx %lu, FunctionName: %s\n", resourceIdx, fcName.c_str());
          _rootInstanceMachine = machine;
        } 

        // Advancing current instance
        currentMachineIdx++;
      }

    // The root instance runs its own function now
    const auto& rootInstanceFcName = _rootInstanceMachine.getFunction();
    const auto& rootInstanceFc = _registeredFunctions[rootInstanceFcName];
    // printf("Root Function Name: %s\n", rootInstanceFcName.c_str());
    rootInstanceFc();
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

  __INLINE__ void registerFunction(const std::string& functionName, std::function<void()> fc)
  {
    // Checking if the RPC name was already used
    if (_registeredFunctions.contains(functionName) == true)
    {
        fprintf(stderr, "The function '%s' was already registered.\n", functionName.c_str());
        abort();
    } 

    // Adding new RPC to the set
    _registeredFunctions.insert({functionName, fc});
  }
  __INLINE__ size_t getInstanceCount() const { return _engine->getInstanceCount(); }
  __INLINE__ void finalize() { _engine->finalize(); }
  __INLINE__ void abort() { _engine->abort(); }
  __INLINE__ bool isRootInstance() const { return _engine->isRootInstance(); }

  private:

  __INLINE__ void launchFunction(const size_t resourceIdx, const std::string& functionName)
  {
    if (_registeredFunctions.contains(functionName) == false)
    {
        fprintf(stderr, "The function RPC '%s' is not registered. Please register it before initializing DeployR.\n", functionName.c_str());
        abort();
    } 

    _engine->launchRPC(resourceIdx, functionName);
  }

  bool _continueExecuting = true;
  std::unique_ptr<Engine> _engine;
  Deployment _deployment;

  std::map<std::string, std::function<void()>> _registeredFunctions;
  size_t _rootInstanceIdx;
  Request::Machine _rootInstanceMachine;

}; // class DeployR

} // namespace deployr