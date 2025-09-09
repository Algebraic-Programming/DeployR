
#pragma once

#include <hicr/core/exceptions.hpp>
#include <hicr/core/definitions.hpp>
#include <nlohmann_json/json.hpp>
#include <nlohmann_json/parser.hpp>
#include <hopcroft_karp/hopcroft_karp.hpp>
#include <hicr/backends/pthreads/computeManager.hpp>
#include <hicr/frontends/RPCEngine/RPCEngine.hpp>
#include <algorithm>
#include <vector>
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
  DeployR(HiCR::InstanceManager *instanceManager, HiCR::frontend::RPCEngine *rpcEngine, const HiCR::Topology &localTopology)
    : _instanceManager(instanceManager),
      _rpcEngine(rpcEngine),
      _localTopology(localTopology)
  {}

  ~DeployR() = default;

  /**
   * The initialization function for DeployR.
   */
  __INLINE__ void initialize()
  {
    // Registering topology exchanging RPC
    auto gatherTopologyRPC = [this]() {
      // Serializing
      const auto serializedTopology = _localTopology.serialize().dump();

      // Returning serialized topology
      _rpcEngine->submitReturnValue((void *)serializedTopology.c_str(), serializedTopology.size() + 1);
    };

    // Adding RPC
    registerRPC(__DEPLOYR_GET_TOPOLOGY_RPC_NAME, gatherTopologyRPC);
  }

  /**
   * Attempts to deploy a deployment request. 
   * 
   * If not enough (or too many) hosts are detected than the request needs, it will abort execution.
   * If a good mapping is found, it will run each of the requested instance in one of the found hosts and run its initial function.
   * 
   * @param[in] deploymnet A deployment object containing the configuration required to deploy a job
   */
  __INLINE__ void deploy(const Deployment &deployment, const HiCR::Instance::instanceId_t coordinatorInstanceId)
  {
    // Getting this running instance information
    const auto &currentInstance   = _instanceManager->getCurrentInstance();
    const auto  currentInstanceId = currentInstance->getId();

    // Getting runner set
    const auto &runners = deployment.getRunners();

    // Remembering if the coordinator is assigned a runner role
    bool coodinatorIsRunner = false;

    // Gathering required HiCR instances into a set
    std::set<HiCR::Instance::instanceId_t> instanceIds;
    for (const auto &runner : runners) instanceIds.insert(runner.getId());

    // Sanity check: make sure there are no repeated instances being used
    if (runners.size() != instanceIds.size()) HICR_THROW_LOGIC("[DeployR] A repeated HiCR instance was provided.\n");

    // Bifurcation point: this is only run by the non-coordinator instance
    // they are captured until the coordinator syncs up with them
    if (currentInstanceId != coordinatorInstanceId)
    {
      _rpcEngine->listen();
      return;
    }

    // Gathering accessible instances from the instance manager
    const auto                                              &instances = _instanceManager->getInstances();
    std::map<HiCR::Instance::instanceId_t, HiCR::Instance *> instanceMap;
    for (const auto &instance : instances) instanceMap.insert({instance->getId(), instance.get()});

    // Sending RPCs to the paired hosts to start deployment
    for (const auto &runner : runners)
    {
      const auto  runnerId      = runner.getId();
      const auto &initialFcName = runner.getFunction();
      const auto  instanceId    = runner.getInstanceId();

      // Getting instance corresponding to the provided Id
      if (instanceMap.contains(instanceId) == false) HICR_THROW_LOGIC("[DeployR] Provided instance id %lu not found in the instance manager provided.\n", instanceId);
      const auto instance = instanceMap.at(instanceId);

      // If the pairing refers to this host, assign its function name but delay execution
      if (instanceId == currentInstanceId)
      {
        coodinatorIsRunner = true;
        _initialFunction   = initialFcName;
        _runnerId          = runnerId;
        continue;
      }

      // Sending RPC
      _rpcEngine->requestRPC(*instance, initialFcName, runnerId);
    }

    // Running initial function, if one has been assigned to the coordinator
    if (coodinatorIsRunner) runInitialFunction();
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
  [[nodiscard]] __INLINE__ const Runner::runnerId_t getRunnerId() const
  {
    // If I am root, I remembered my instance Id
    if (_instanceManager->getCurrentInstance()->isRootInstance() == true) return _runnerId;

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
  __INLINE__ void abort() { _instanceManager->abort(-1); }

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
  __INLINE__ HiCR::Instance &getCurrentHiCRInstance() const { return *_instanceManager->getCurrentInstance(); }

  /**
 * Gets the global topology, the sum of all local topologies among the provided instances
 * 
 * @return A vector containing each of the local topologies, where the index corresponds to the host index in the getHiCRInstances function
 */
  [[nodiscard]] __INLINE__ std::vector<HiCR::Topology> gatherGlobalTopology(const HiCR::Instance::instanceId_t              rootInstanceId,
                                                                            const std::vector<HiCR::Instance::instanceId_t> instanceIds)
  {
    // Storage
    std::vector<HiCR::Topology> globalTopology;
    const auto                 &currentInstance = _instanceManager->getCurrentInstance();
    const bool                  isRootInstance  = currentInstance->getId() == rootInstanceId;
    const auto                 &instances       = _instanceManager->getInstances();

    // If I am not root and I am among the participating instances, then listen for the incoming RPC and return an empty topology
    if (isRootInstance == false) { _rpcEngine->listen(); }
    else // If I am root, request topology from all instances
    {
      for (const auto &instance : instances)
        if (instance->getId() == currentInstance->getId()) // If its me, just push my local topology
        {
          globalTopology.push_back(_localTopology.serialize());
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

          // Creating new topology object
          HiCR::Topology topology(topologyJson);

          // Pushing topology into the vector
          globalTopology.push_back(topology);
        }
    }

    // Return global topology
    return globalTopology;
  }

  /**
 * Performs a matching between the a set of required topologies and a set of given (existing) topologies and returns, if exists, a possible pairing.
 * 
 * A pairing consists of a 1:1 mapping of the required topologies and the given topologies. For each pairing the given topology is a equal or a superset of the required one.
 * 
 * This implementation uses the Hopcroft-Karp algorithm to find a matching of all requested runners to a host.
 * 
 * @return If successful, a vector of size size(requested) containing the indexes of the given topologies that match the requested ones. Otherwise, an empty vector.
 */
  [[nodiscard]] __INLINE__ static std::vector<size_t> doBipartiteMatching(const std::vector<HiCR::Topology> &requested, const std::vector<HiCR::Topology> &given)
  {
    // Creating pairings vector
    std::vector<size_t> pairingsVector;

    // Creating one deployment runner per requested runner
    for (size_t i = 0; i < requested.size(); i++) pairingsVector.push_back(i);

    // Building the matching graph
    theAlgorithms::graph::HKGraph graph(pairingsVector.size(), given.size());
    for (size_t i = 0; i < pairingsVector.size(); i++)
      for (size_t j = 0; j < given.size(); j++)
        if (HiCR::Topology::isSubset(given[j], requested[i])) graph.addEdge(i, j);

    //  Finding out if a proper matching exists
    auto matchCount = (size_t)graph.hopcroftKarpAlgorithm();

    // If the number of matchings is smaller than requested, return an empty vector
    if (matchCount < pairingsVector.size()) return {};

    // Getting the pairings from the graph
    const auto graphPairings = graph.getLeftSidePairings();
    for (size_t i = 0; i < pairingsVector.size(); i++)
    {
      const auto givenIdx = (size_t)graphPairings[i + 1];
      pairingsVector[i]   = givenIdx;
    }

    return pairingsVector;
  }

  private:

  __INLINE__ std::shared_ptr<HiCR::Instance> createInstance(const HiCR::InstanceTemplate t)
  {
    std::shared_ptr<HiCR::Instance> newInstance;
    try
    {
      newInstance = _instanceManager->createInstance(t);
    }
    catch (const std::exception &e)
    {
      HICR_THROW_FATAL("[DeployR] Failed to create new instance. Reason: \n  + '%s'", e.what());
    }

    if (newInstance.get() == nullptr) HICR_THROW_FATAL("Failed to create new instance with requested topology: %s\n", t.getTopology().serialize().dump(2).c_str());

    return newInstance;
  }

  /**
 * [Internal] Runs the initial function assigned to this instance
 */
  __INLINE__ void runInitialFunction()
  {
    // Checking the requested function was registered
    if (_registeredFunctions.contains(_initialFunction) == false)
      HICR_THROW_FATAL("The requested function name '%s' is not registered. Please register it before initializing DeployR.\n", _initialFunction.c_str());

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
  * Indicates whether the local instance is the HiCR root instance
  * 
  * @return true, if this is the root instance; false, otherwise.
  */
  [[nodiscard]] __INLINE__ bool isRootInstance() const { return getCurrentHiCRInstance().getId() == _instanceManager->getRootInstanceId(); }

  /// Deployment instance id that this HiCR instance
  Runner::runnerId_t _runnerId;

  /// The initial function this instance needs to run
  std::string _initialFunction;

  /// A map of registered functions, targets for an instance's initial function
  std::map<std::string, std::function<void()>> _registeredFunctions;

  // Externally-provided Instance Manager to use
  HiCR::InstanceManager *const _instanceManager;

  /// The RPC engine to use for all remote function requests
  HiCR::frontend::RPCEngine *const _rpcEngine;

  /// Storage for the local system topology
  HiCR::Topology _localTopology;

}; // class DeployR

} // namespace deployr