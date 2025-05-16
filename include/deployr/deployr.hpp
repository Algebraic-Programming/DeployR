
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
    // Counting the exact number of instances requested.
    size_t instancesRequested = request.getInstances().size();

    // Getting the initial number of instances
    size_t initialInstanceCount = _engine->getInstanceCount();

    // Printing instance count information
    // printf("[DeployR] Initial Instances:   %lu\n", initialInstanceCount);
    // printf("[DeployR] Requested Instances: %lu\n", instancesRequested);

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
      //printf("%s\n", globalTopology[i].dump(2).c_str());
      //printf("--------\n");
    // }

    // Building deployment object
    Deployment deployment(request);

    // Adding hosts to the deployment object
    for (size_t i = 0; i < globalTopology.size(); i++) deployment.addHost(Host(i, globalTopology[i]));

    // Proceed with request to instance matching
    if (deployment.performMatching() == false)
    {
      fprintf(stderr, "[DeployR] The provided resources are not sufficient for the requested instances.\n");
      _engine->abort();
    } 

    // Getting root instance index
    _rootInstanceIdx = _engine->getRootInstanceIndex();
    //printf("Root Instance Idx: %lu\n", _rootInstanceIdx);

    // Storage for the name of the initial function name for the root instance
    std::string rootInstanceFcName;

    // Launching initial function to each of the requested instances
    for (const auto& pairing : deployment.getPairings())
    {
      // Getting the destination resource idx paired to this instance request
      const auto hostIndex = pairing.getAssignedHostIndex();

      // Getting requested instance's name
      const auto& requestedInstanceName = pairing.getRequestedInstanceName();

      // Getting requested instance's information
      const auto& requestedInstance = request.getInstances().at(requestedInstanceName);

      // Getting the function to run for the paired instance
      const auto fcName = requestedInstance.getFunction();
      
      // Checking the requested function was registered
      if (_registeredFunctions.contains(fcName) == false)
      {
          fprintf(stderr, "The requested function name '%s' is not registered. Please register it before initializing DeployR.\n", fcName.c_str());
          abort();
      } 
      
      
      // Launching initial function in the destination instance
      // If the resource index is the root instance, then don't send RPC. It will be executed manually
      if (hostIndex != _rootInstanceIdx)
      {
        // printf("Launching RPC: ResourceIdx %lu, FunctionName: %s\n", resourceIdx, fcName.c_str());
        launchFunction(hostIndex, fcName);
      } 
      else
      {
        // printf("Running Root: ResourceIdx %lu, FunctionName: %s\n", resourceIdx, fcName.c_str());
        rootInstanceFcName = fcName;
      } 
    }

    // The root instance runs its own function now
    const auto& rootInstanceFc = _registeredFunctions[rootInstanceFcName];

    // printf("Root Function Name: %s\n", rootInstanceFcName.c_str());
    rootInstanceFc();

    printf("Deployment Size: %lu (binary: %lu)", deployment.serialize().dump().size(), nlohmann::json::to_cbor(deployment.serialize()).size());
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

  std::map<std::string, std::function<void()>> _registeredFunctions;
  size_t _rootInstanceIdx;

}; // class DeployR

} // namespace deployr