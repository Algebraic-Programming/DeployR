
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
#define __DEPLOYR_GET_DEPLOYMENT_RPC_NAME "[DeployR] Get Deployment"

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
    registerFunction(__DEPLOYR_GET_TOPOLOGY_RPC_NAME, [this]() {
      // Serializing
      const auto serializedTopology = _localTopology.serialize().dump();

      // Returning serialized topology
      _rpcEngine->submitReturnValue((void *)serializedTopology.c_str(), serializedTopology.size() + 1);
    });

    // Registering deployment broadcasting RPC
    registerFunction(__DEPLOYR_GET_DEPLOYMENT_RPC_NAME, [this]() {
      // Serializing
      const auto serializedDeployment = _deployment.serialize().dump();

      // Returning serialized topology
      _rpcEngine->submitReturnValue((void *)serializedDeployment.c_str(), serializedDeployment.size() + 1);
    });
  }

  ~DeployR() = default;

  /**
   * The initialization function for DeployR.
   * 
   * It initializes the HiCR management engine, detects the local topology, broadcasts the global topology to the root instance.
   * All instances need to be call this function before the root instance can request a deployment. Only the root instance will continue thereafter. 
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
  __INLINE__ void deploy(Request &request)
  {
    // Bifurcation point: this is only run by the non-coordinator instance
    // they are captured until the coordinator syncs up with them
    if (this->getCurrentInstance().isRootInstance() == false) 
    {
      // Running initial deployment operations
      initialDeployment();

      // Returning immediately
      return;
    }

    // Counting the exact number of instances requested.
    size_t instancesRequested = request.getInstances().size();

    // Getting the initial number of instances
    size_t HiCRInstanceCount = _instanceManager->getInstances().size();

    // Printing instance count information
    // printf("[DeployR] Initial Instances:   %lu\n", HiCRInstanceCount);
    // printf("[DeployR] Requested Instances: %lu\n", instancesRequested);

    // We handle the following scenarios:
    // With N: number of requested instances
    // With K: number of initial instances

    // If K > 1 && K < N, this is an irregular start, which is not handled. Abort
    if (HiCRInstanceCount > 1 && HiCRInstanceCount < instancesRequested)
    {
      fprintf(stderr, "[DeployR] More initial instances (%lu) provided than required (%lu) were provided.\n", HiCRInstanceCount, instancesRequested);
      _instanceManager->abort(-2);
    }

    // If K > N, more initial instances than requested provided. Abort execution.
    if (HiCRInstanceCount > instancesRequested)
    {
      fprintf(stderr, "[DeployR] More initial instances (%lu) provided than required (%lu) were provided.\n", HiCRInstanceCount, instancesRequested);
      _instanceManager->abort(-3);
    }

    // If K == 1, this is the cloud scenario. The rest of the instances will be created.
    if (HiCRInstanceCount == 1) deployRemainingInstances(request);

    // Updating number of instances available
    HiCRInstanceCount = _instanceManager->getInstances().size();

    // Building deployment object
    _deployment = Deployment(request);

    // Exchanging information with other instances by running the initial deployment function
    initialDeployment();

    // Adding hosts to the deployment object
    for (size_t i = 0; i < _globalTopology.size(); i++)
    {
      // Getting instance id
      const auto instanceId = _instanceManager->getInstances()[i]->getId();
      
      // Adding corresponding host
      _deployment.addHost(Host(instanceId, _globalTopology[i]));
    } 

    // Proceed with request to instance matching
    if (_deployment.performMatching() == false)
    {
      fprintf(stderr,
              "[DeployR] The detected hosts (%lu) are either not sufficient for the requested instances (%lu) or their topology doesn't satisfy that of the requested instances.\n",
              HiCRInstanceCount,
              instancesRequested);
      _instanceManager->abort(-4);
    }

    // Broadcasting deployment information to non-root instances
    broadcastDeployment();

    // Identifying this local instance
    _localInstance = identifyLocalInstance();

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
    if (_registeredFunctions.contains(functionName) == true)
    {
      fprintf(stderr, "The function '%s' was already registered.\n", functionName.c_str());
      _instanceManager->abort(-5);
    }

    // Adding new RPC to the set
    _registeredFunctions.insert({functionName, fc});

    // Registering the function on the engine
    registerRPC(functionName, fc);
  }

  /**
   * Retrieves a deployment object with the information about the deployment (pairings, hosts, request)
   * 
   * This function must be called only after deploying
   * 
   * @return The deployment object
   */
  [[nodiscard]] __INLINE__ const Deployment &getDeployment() const { return _deployment; }

  /**
   * Retrieves the instance request corresponding to the local running instance
   * 
   * This function must be called only after initializing
   * 
   * @return The instance request object
   */
  [[nodiscard]] __INLINE__ const Request::Instance &getLocalInstance() const { return _localInstance; }

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
     _instanceManager->abort(-5);
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
  __INLINE__ HiCR::Instance& getCurrentInstance() { return *_instanceManager->getCurrentInstance(); }

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


  // Function for engine deployment
  __INLINE__ void initialDeployment()
  {
    // Gathering global topology into the root instance
    _globalTopology = gatherGlobalTopology();

    // If this is not the root instance, wait for incoming RPCs
    if (isRootInstance() == false)
    {
      // Getting deployment information from the root instance
      _deployment = broadcastDeployment();
      // printf("Deployment Size: %lu (binary: %lu)\n", _deployment.serialize().dump().size(), nlohmann::json::to_cbor(_deployment.serialize()).size());

      // Identifying local instance
      _localInstance = identifyLocalInstance();

      // Running initial function assigned to this host
      runInitialFunction();
    }
  }

  /**
   * [Internal] Launches a provided function on the requested host
   * 
   * @param[in] hostIdx The index within the HiCR instanceList_t corresponding to the host that should execute the function (RPC)
   * @param[in] functionName The name of the function to run. It must be registered on the target host before running
   */
  __INLINE__ void launchFunction(const size_t hostIdx, const std::string &functionName)
  {
    if (_registeredFunctions.contains(functionName) == false)
    {
      fprintf(stderr, "The function RPC '%s' is not registered. Please register it before initializing DeployR.\n", functionName.c_str());
      _instanceManager->abort(-7);
    }

    launchRPC(hostIdx, functionName);
  }

  /**
   * [Internal] Gets the global topology, the sum of all local topologies
   * 
   * @return A vector containing each of the local topologies, where the index corresponds to the host index in the getHiCRInstances function
   */
  [[nodiscard]] __INLINE__ std::vector<nlohmann::json> gatherGlobalTopology()
  {
    // Storage
    std::vector<nlohmann::json> globalTopology;

    // If I am not root, listen for the incoming RPC and return an empty topology
    if (isRootInstance() == false) _rpcEngine->listen();

    // If I am root, request topology from all instances
    else
      for (const auto &instance : _instanceManager->getInstances())

        // If its the root instance (me), just push my local topology
        if (instance->isRootInstance() == true) globalTopology.push_back(_localTopology.serialize());

        // If not, it's another instance: send RPC and deserialize return value
        else
        {
          // Requesting RPC from the remote instance
          launchRPC(*instance, __DEPLOYR_GET_TOPOLOGY_RPC_NAME);

          // Getting return value as a memory slot
          auto returnValue = _rpcEngine->getReturnValue(*instance);

          // Receiving raw serialized topology information from the worker
          std::string serializedTopology = (char *)returnValue->getPointer();

          // Parsing serialized raw topology into a json object
          auto topologyJson = nlohmann::json::parse(serializedTopology);

          // Freeing return value
          _rpcEngine->getMemoryManager()->freeLocalMemorySlot(returnValue);

          // Pushing topology into the vector
          globalTopology.push_back(topologyJson);
        }

    // Return global topology
    return globalTopology;
  }

  /**
   * [Internal] Makes the root instance broadcast the serialized deployment information to all others and deserializes it locally.
   * 
   * @return The deserialized Deployment object
   */
  __INLINE__ Deployment broadcastDeployment()
  {
    // Storage
    Deployment deployment;

    // If I am root
    if (isRootInstance() == true)
    {
      // listen for the incoming RPCs and return an empty topology
      for (size_t i = 0; i < _instanceManager->getInstances().size() - 1; i++) _rpcEngine->listen();

      // Copy my own deployment information
      deployment = _deployment;
    }

    // If I am not root, request deployment from root
    else
    {
      // Requessting RPC from the remote instance
      launchRPC(getRootInstanceIndex(), __DEPLOYR_GET_DEPLOYMENT_RPC_NAME);

      // Getting return value as a memory slot
      auto returnValue = _rpcEngine->getReturnValue(getRootInstance());

      // Receiving raw serialized topology information from the worker
      std::string serializedDeployment = (char *)returnValue->getPointer();

      // Parsing serialized raw topology into a json object
      auto deploymentJson = nlohmann::json::parse(serializedDeployment);

      // Freeing return value
      _rpcEngine->getMemoryManager()->freeLocalMemorySlot(returnValue);

      // Pushing topology into the vector
      deployment = Deployment(deploymentJson);
    }

    // Return global topology
    return deployment;
  }

  /**
   * [Internal] Detects which of the instances in the request corresponds to this local instance
   * 
   * @return An instance request object corresponding to the local instance
   */
  __INLINE__ deployr::Request::Instance identifyLocalInstance()
  {
    // Getting pairings
    const auto &pairings = _deployment.getPairings();

    // Getting host list
    const auto &hostList = _deployment.getHosts();

    // Finding the pairing corresponding to this host
    std::string localInstanceName;
    for (const auto &p : pairings)
    {
      const auto& instanceId = hostList[p.second].getInstanceId();
      if (instanceId == getCurrentInstance().getId())
      {
        localInstanceName = p.first;
        break;
      }
    }

    // Getting requested instance's information
    return _deployment.getRequest().getInstances().at(localInstanceName);
  }

  /**
 * [Internal] Runs the initial function assigned to this instance
 */
  __INLINE__ void runInitialFunction()
  {
    // Getting the function to run for the paired instance
    const auto fcName = _localInstance.getFunction();

    // Checking the requested function was registered
    if (_registeredFunctions.contains(fcName) == false)
    {
      fprintf(stderr, "The requested function name '%s' is not registered. Please register it before initializing DeployR.\n", fcName.c_str());
      _instanceManager->abort(-8);
    }

    // Getting function pointer
    const auto &initialFc = _registeredFunctions[fcName];

    // Running initial function
    initialFc();
  }

  __INLINE__ void deployRemainingInstances(Request &request)
  {
    // Making a copy of the requested instances. One of them will be satisfied (and removed) by the currently running instance
    auto requestedInstances = request.getInstances();

    // Getting requested host types
    const auto& requestedHostTypes = request.getHostTypes();

    // Looking for requested instances that are satisfied by the currently running instances
    for (auto requestedInstanceItr = requestedInstances.begin();  requestedInstanceItr != requestedInstances.end(); requestedInstanceItr++)
    {
      const auto& requestedInstance = requestedInstanceItr->second;
      // const auto& requestedInstanceName = requestedInstance.getName();
      const auto& requestedInstanceHostType = requestedInstance.getHostType();
      const auto& requestedHostType = requestedHostTypes.at(requestedInstanceHostType);
      
      // Checking for compatibility
      deployr::Host localHost(0, _localTopology.serialize());
      const bool isCompatible = localHost.checkCompatibility(requestedHostType);
      // printf("Instance %s with type %s is %scompatible\n", requestedInstanceName.c_str(), requestedHostType.getName().c_str(), isCompatible ? "" : "not ");
      
      // If it is compatible, then:  
      if (isCompatible)
      {
        // Remove it from the list of new instances to request
        requestedInstances.erase(requestedInstanceItr);

        // and break the loop
        break;
      }
    }

    // Requesting the creation of the rest of the instances
    for (const auto& requestedInstanceItr : requestedInstances)
    {
      const auto& requestedInstance = requestedInstanceItr.second;
      // const auto& requestedInstanceName = requestedInstance.getName();
      const auto& requestedInstanceHostType = requestedInstance.getHostType();
      const auto& requestedHostType = requestedHostTypes.at(requestedInstanceHostType);

      const auto requestedTopology = HiCR::Topology(requestedHostType.getTopology());
      const auto requestedTemplate = HiCR::InstanceTemplate(requestedTopology);

      // Creating new instance
      auto newInstance = createInstance(requestedTemplate);
    }

    // printf("Active instances: %lu\n", _instanceManager->getInstances().size() );
  }

  /**
  * Function to request the execution of an RPC in a remote instance
  * 
  * @param[in] instance HiCR instance to execute this RPC
  * @param[in] RPCName Name of the function to run. It must have been registered in the target instance beforehand to work
  */
  __INLINE__ void launchRPC(HiCR::Instance& instance, const std::string &RPCName)
  {
    _rpcEngine->requestRPC(instance, RPCName);
  }

  /**
    * Function to request the execution of an RPC in a remote instance
    * 
    * @param[in] instanceIndex Index within the HiCR instance list corresponding to the instance that must execute this RPC
    * @param[in] RPCName Name of the function to run. It must have been registered in the target instance beforehand to work
    */
  __INLINE__ void launchRPC(const size_t instanceIndex, const std::string &RPCName)
  {
    auto &instances = _instanceManager->getInstances();
    auto &instance  = instances[instanceIndex];
    this->launchRPC(*instance, RPCName);
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
  [[nodiscard]] __INLINE__ bool isRootInstance() const { return _instanceManager->getCurrentInstance()->getId() == _instanceManager->getRootInstanceId(); }

  /// A map of registered functions, targets for an instance's initial function
  std::map<std::string, std::function<void()>> _registeredFunctions;

  /// Local instance object
  Request::Instance _localInstance;

  /// Storage for the global system topology
  std::vector<nlohmann::json> _globalTopology;

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