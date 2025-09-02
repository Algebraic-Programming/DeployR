
#pragma once

#include <hicr/core/exceptions.hpp>
#include <hicr/core/definitions.hpp>
#include <nlohmann_json/json.hpp>
#include <nlohmann_json/parser.hpp>
#include <hicr/backends/pthreads/computeManager.hpp>
#include <hicr/frontends/RPCEngine/RPCEngine.hpp>
#include <algorithm>
#include <vector>
#include "request.hpp"
#include "deployment.hpp"

#define __DEPLOYR_GET_TOPOLOGY_RPC_NAME "[DeployR] Get Topology"

namespace deployr
{

/**
 * Main runtime class of DeployR. This is the only entry point for end-users. It provides the functionality to deploy a deployment request
 */
class DeployR final
{
  public:

  /**
   * Default constructor for DeployR. It creates the HiCR management engine and registers the basic functions needed during deployment.
   */
  DeployR(
    HiCR::InstanceManager* instanceManager,
    HiCR::frontend::RPCEngine* rpcEngine,
    const HiCR::Topology& localTopology)
  : 
    _instanceManager(instanceManager),
    _rpcEngine(rpcEngine),
    _localTopology(localTopology)
  {
    // Registering topology exchanging RPC
    registerRPC(__DEPLOYR_GET_TOPOLOGY_RPC_NAME, [this]() {
      // Serializing
      const auto serializedTopology = _localTopology.serialize().dump();

      // Returning serialized topology
      _rpcEngine->submitReturnValue((void *)serializedTopology.c_str(), serializedTopology.size() + 1);
    });
  }

  ~DeployR() = default;

  /**
   * The initialization function for DeployR.
   * 
   * @return True, if this is the root rank; False, otherwise
   */
  __INLINE__ void initialize()
  {
  }

  /**
   * Attempts to deploy a deployment request. 
   * 
   * If not enough (or too many) hosts are detected than the request needs, it will abort execution.
   * If a good mapping is found, it will run each of the requested instance in one of the found hosts and run its initial function.
   * 
   * @param[in] request A request object containing all the required instances that make a deployment.
   */
  __INLINE__ void deploy(Request &request, const std::vector<HiCR::Instance*> instances)
  {
    // Gatherince instance ids into a set
    std::set<HiCR::Instance::instanceId_t> instanceIds;
    for (const auto& instance : instances) instanceIds.insert(instance->getId());

    // If I am not among the participating instances, simply return
    if (instanceIds.contains(_instanceManager->getCurrentInstance()->getId()) == false) return;

    // Designating coordinator instance for the deployment
    const auto coordinatorInstanceId = *instanceIds.begin();

    // Gathering global topology into the root instance
    _globalTopology = gatherGlobalTopology(instances);

    // Bifurcation point: this is only run by the non-coordinator instance
    // they are captured until the coordinator syncs up with them
    if (getCurrentHiCRInstance().getId() != coordinatorInstanceId) { _rpcEngine->listen(); return; }

    // Counting the exact number of instances requested.
    size_t instancesRequested = request.getInstances().size();

    // Getting the number of provided instances
    size_t providedInstanceCount = instances.size();

    // If K != N, a different number instances provided than requested. 
    if (providedInstanceCount != instancesRequested) HICR_THROW_LOGIC("[DeployR] Different number of instances (%lu) required than provided (%lu).", providedInstanceCount, instancesRequested);

    // Building deployment object
    _deployment = Deployment(request);

    // Adding hosts to the deployment object
    for (size_t i = 0; i < instances.size(); i++)
    {
      // Getting instance id
      const auto instanceId = instances[i]->getId();
      
      // Adding corresponding host
      _deployment.addHost(Host(instanceId, _globalTopology[instanceId]));
    } 

    // Proceed with request to instance matching
    if (_deployment.performMatching() == false)
    {
      HICR_THROW_LOGIC("[DeployR] The provided HiCR instances (%lu) are either not sufficient for the requested instances (%lu) or their topology doesn't satisfy that of the requested instances.",
              providedInstanceCount,
              instancesRequested);
    }

    // Sending RPCs to the paired hosts to start deployment
    for (const auto& pairing : _deployment.getPairings())
    {
       const auto instanceId = pairing.first;
       const auto& initialFcName = request.getInstances().at(instanceId).getFunction();
       const auto hostIdx = pairing.second;
       const auto& host = _instanceManager->getInstances().at(hostIdx);
       const auto hostInstanceId = host->getId();
       
       // If the pairing refers to this host, assign its function name but delay execution
       if (hostInstanceId == _instanceManager->getCurrentInstance()->getId())
       {
        _initialFunction = initialFcName;
        _instanceId = instanceId;
        continue;
       }

       // Sending RPC
      _rpcEngine->requestRPC(*host, initialFcName, instanceId);
    }

    // Running initial function assigned to this host
    runInitialFunction();
  }

  /**
   * Registers a function that can be a target as initial function for one or more requested instances.
   * 
   * If a requested initial function is not registered by this function, deployment will fail.
   * 
   * @param[in] functionName The name of the function to register. This value will be used to match against the requested instance functions
   * @param[in] fc The actual function to register
   * 
   */
  __INLINE__ void registerFunction(const std::string &functionName, std::function<void()> fc)
  {
    // Checking if the RPC name was already used
    if (_registeredFunctions.contains(functionName) == true) HICR_THROW_LOGIC("The function '%s' was already registered.", functionName.c_str());

    // Adding new RPC to the set
    _registeredFunctions.insert({functionName, fc});

    // Adding function to RPC Engine
    registerRPC(functionName, fc);
  }

  /**
   * Retrieves the id of the running instance
   * 
   * @return The id of the running instance
   */
  [[nodiscard]] __INLINE__ const Request::Instance::instanceId_t getInstanceId() const
  {
    // If I am root, I remembered my instance Id
    if (_instanceManager->getCurrentInstance()->isRootInstance() == true) return _instanceId;

    // Otherwise, get it from the RPC engine
    return _rpcEngine->getRPCArgument();
  }

  /**
   * Finalizes the deployment. Must be called by the root instance before exiting the applicataion
   */
  __INLINE__ void finalize()
  { 
     // Nothing to do here so far
  }

  /**
   * Fatally aborts execution. Must be used only in case of unsalvageable errors.
   */
  __INLINE__ void abort()
  {
     _instanceManager->abort(-1);
  }

  /**
   * Retrieves the RPC engine for direct use
   * 
   * @return A pointer to the internal RPC engine
   */
  __INLINE__ HiCR::frontend::RPCEngine *getRPCEngine() { return _rpcEngine; }

    /**
   * Retrieves currently running instance
   * 
   * @return The current HiCR instance
   */
  __INLINE__ HiCR::Instance& getCurrentHiCRInstance() const { return *_instanceManager->getCurrentInstance(); }

  private:

  __INLINE__ std::shared_ptr<HiCR::Instance> createInstance(const HiCR::InstanceTemplate t)
  {
      std::shared_ptr<HiCR::Instance> newInstance;
      try
      {
        newInstance = _instanceManager->createInstance(t);
      }
      catch(const std::exception& e)
      {
        HICR_THROW_FATAL("[DeployR] Failed to create new instance. Reason: \n  + '%s'", e.what());
      }
      
      if (newInstance.get() == nullptr)
      {
        fprintf(stderr, "Failed to create new instance with requested topology: %s\n", t.getTopology().serialize().dump(2).c_str());
        abort();
      }

      return newInstance;
  }

  /**
   * [Internal] Gets the global topology, the sum of all local topologies
   * 
   * @return A vector containing each of the local topologies, where the index corresponds to the host index in the getHiCRInstances function
   */
  [[nodiscard]] __INLINE__ std::map<HiCR::Instance::instanceId_t, nlohmann::json> gatherGlobalTopology(const std::vector<HiCR::Instance*>& instances)
  {
    // Storage
    std::map<HiCR::Instance::instanceId_t, nlohmann::json> globalTopology;

    // My instance Id
    const auto currentHiCRInstanceId = getCurrentHiCRInstance().getId();

    // If I am not root and I am among the participating instances, then listen for the incoming RPC and return an empty topology
    if (isRootInstance() == false)
    {
       _rpcEngine->listen();
    }
    else // If I am root, request topology from all instances
    {
      for (const auto instance : instances)
        if (instance->getId() == currentHiCRInstanceId) // If its me, just push my local topology
        {
         globalTopology.insert( { currentHiCRInstanceId, _localTopology.serialize() } );
        }
        else // If not, it's another instance: send RPC and deserialize return value
        {
          // Requesting RPC from the remote instance
          _rpcEngine->requestRPC(*instance, __DEPLOYR_GET_TOPOLOGY_RPC_NAME);

          // Getting return value as a memory slot
          auto returnValue = _rpcEngine->getReturnValue(*instance);

          // Receiving raw serialized topology information from the worker
          std::string serializedTopology = (char *)returnValue->getPointer();

          // Parsing serialized raw topology into a json object
          auto topologyJson = nlohmann::json::parse(serializedTopology);

          // Freeing return value
          _rpcEngine->getMemoryManager()->freeLocalMemorySlot(returnValue);

          // Pushing topology into the vector
          globalTopology.insert({instance->getId(), topologyJson});
        }
    }

    // Return global topology
    return globalTopology;
  }

  /**
 * [Internal] Runs the initial function assigned to this instance
 */
  __INLINE__ void runInitialFunction()
  {
    // Checking the requested function was registered
    if (_registeredFunctions.contains(_initialFunction) == false)
    {
      fprintf(stderr, "The requested function name '%s' is not registered. Please register it before initializing DeployR.\n", _initialFunction.c_str());
      _instanceManager->abort(-8);
    }

    // Getting function pointer
    const auto &initialFc = _registeredFunctions[_initialFunction];

    // Running initial function
    initialFc();
  }

  /**
  * Registers a function as target for an RPC
  * 
  * @param[in] RPCName The name of the function to register
  * @param[in] fc The actual function to register
  */
  __INLINE__ void registerRPC(const std::string &RPCName, std::function<void()> fc)
  {
    // Registering RPC
    auto RPCExecutionUnit = HiCR::backend::pthreads::ComputeManager::createExecutionUnit([fc](void *) { fc(); });

    // Adding RPC
    _rpcEngine->addRPCTarget(RPCName, RPCExecutionUnit);
  }

  /**
    * Gets the index within the HiCR instance list corresponding to the root instance
    * 
    * @return The index within the HiCR instance list corresponding to the root instance
    */
  [[nodiscard]] __INLINE__ size_t getRootInstanceIndex() const
  {
    const auto &instances = _instanceManager->getInstances();
    for (size_t i = 0; i < instances.size(); i++)
      if (instances[i]->isRootInstance()) return i;
    return 0;
  }

  /**
  * Gets the HiCR instance object corresponding to the root instance
  * 
  * @return A HiCR instance object corresponding to the root instance
  */
__INLINE__ HiCR::Instance &getRootInstance() const
  {
    auto &instances = _instanceManager->getInstances();
    for (size_t i = 0; i < instances.size(); i++)
      if (instances[i]->isRootInstance()) return *(instances[i]);
    return *(instances[0]);
  }

  /**
  * Indicates whether the local instance is the HiCR root instance
  * 
  * @return true, if this is the root instance; false, otherwise.
  */
  [[nodiscard]] __INLINE__ bool isRootInstance() const { return getCurrentHiCRInstance().getId() == _instanceManager->getRootInstanceId(); }

  /// Request instance id that this HiCR instance
  Request::Instance::instanceId_t _instanceId;

  /// The initial function this instance needs to run
  std::string _initialFunction;

  /// A map of registered functions, targets for an instance's initial function
  std::map<std::string, std::function<void()>> _registeredFunctions;

  /// Storage for the global system topology
  std::map<HiCR::Instance::instanceId_t, nlohmann::json> _globalTopology;

  // Object containing the information of the deployment
  Deployment _deployment;

  // Externally-provided Instance Manager to use
  HiCR::InstanceManager* const _instanceManager;

  /// The RPC engine to use for all remote function requests
  HiCR::frontend::RPCEngine* const _rpcEngine;

  /// Storage for the local system topology
  HiCR::Topology _localTopology;

}; // class DeployR

} // namespace deployr