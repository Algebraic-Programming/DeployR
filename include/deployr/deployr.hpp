
#pragma once

#include <hicr/core/exceptions.hpp>
#include <hicr/core/definitions.hpp>
#include <nlohmann_json/json.hpp>
#include <nlohmann_json/parser.hpp>
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
  DeployR(
    HiCR::InstanceManager* instanceManager,
    HiCR::frontend::RPCEngine* rpcEngine,
    const HiCR::Topology& localTopology)
  : 
    _instanceManager(instanceManager),
    _rpcEngine(rpcEngine),
    _localTopology(localTopology)
  {

  }

  ~DeployR() = default;

  /**
   * The initialization function for DeployR.
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
   * @param[in] deploymnet A deployment object containing the configuration required to deploy a job
   */
  __INLINE__ void deploy(const Deployment& deployment)
  {
    // Getting this running instance information
    const auto& currentInstance = _instanceManager->getCurrentInstance();
    const auto currentInstanceId = currentInstance->getId();

    // Getting runner set
    const auto& runners = deployment.getRunners();

    // Gathering required HiCR instances into a set
    std::set<HiCR::Instance::instanceId_t> instanceIds;
    for (const auto& runner : runners) instanceIds.insert(runner.getId());

    // Sanity check: make sure there are no repeated instances being used
    if (runners.size() != instanceIds.size()) HICR_THROW_LOGIC("[DeployR] A repeated HiCR instance was provided.\n");

    // If I am not among the participating instances, simply return
    if (instanceIds.contains(currentInstanceId) == false) return;

    // Designating initial runner as coordinator
    const auto& coordinatorRunner = runners[0];
    const auto coordinatorInstanceId = coordinatorRunner.getInstanceId();

    // Bifurcation point: this is only run by the non-coordinator instance
    // they are captured until the coordinator syncs up with them
    if (currentInstanceId != coordinatorInstanceId) { _rpcEngine->listen(); return; }

    // Gathering accessible instances from the instance manager
    const auto& instances = _instanceManager->getInstances();
    std::map<HiCR::Instance::instanceId_t, HiCR::Instance*> instanceMap;
    for (const auto& instance : instances) instanceMap.insert({instance->getId(), instance.get()});

    // Sending RPCs to the paired hosts to start deployment
    for (const auto& runner : runners) 
    {
       const auto runnerId = runner.getId();
       const auto& initialFcName = runner.getFunction();
       const auto instanceId = runner.getInstanceId();
       
       // Getting instance corresponding to the provided Id
       if (instanceMap.contains(instanceId) == false) HICR_THROW_LOGIC("[DeployR] Provided instance id %lu not found in the instance manager provided.\n", instanceId);
       const auto instance = instanceMap.at(instanceId);

       // If the pairing refers to this host, assign its function name but delay execution
       if (instanceId == currentInstanceId)
       {
        _initialFunction = initialFcName;
        _runnerId = runnerId;
        continue;
       }

      // Sending RPC
      _rpcEngine->requestRPC(*instance, initialFcName, runnerId);
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
  [[nodiscard]] __INLINE__ const Runner::runnerId_t getInstanceId() const
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
  HiCR::InstanceManager* const _instanceManager;

  /// The RPC engine to use for all remote function requests
  HiCR::frontend::RPCEngine* const _rpcEngine;

  /// Storage for the local system topology
  HiCR::Topology _localTopology;

}; // class DeployR

} // namespace deployr