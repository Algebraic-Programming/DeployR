
#pragma once

#include <functional>
#include <hicr/core/exceptions.hpp>
#include <hicr/core/definitions.hpp>
#include <hicr/core/instanceManager.hpp>
#include <hicr/backends/pthreads/computeManager.hpp>
#include <hicr/frontends/RPCEngine/RPCEngine.hpp>
#include "instance.hpp"

namespace HiCR::backend::cloudr
{

class CommunicationManager;
class TopologyManager;

/**
 * Instance manager for CloudR
*/
class InstanceManager final : public HiCR::InstanceManager
{
  friend TopologyManager;
  friend CommunicationManager;

  public:

#define __CLOUDR_GATHER_TOPOLOGIES_RPC_NAME "[CloudR] Gather Topologies"
#define __CLOUDR_LAUNCH_ENTRY_POINT_RPC_NAME "[CloudR] Launch Main"
#define __CLOUDR_RELINQUISH_INSTANCE_RPC_NAME "[CloudR] Relinquish Instance"
#define __CLOUDR_FINALIZE_WORKER_RPC_NAME "[CloudR] Finalize Worker"
#define __CLOUDR_EXCHANGE_GLOBAL_MEMORY_SLOTS_RPC_NAME "[CloudR] Exchange Global Memory Slots"
#define __CLOUDR_FENCE_RPC_NAME "[CloudR] Fence"

  /**
   * Type for any entrypoint function
  */
  typedef std::function<void(void)> entryPoint_t;

  /**
   * Constructor
   * 
   * @param[in] rpcEngine
   * @param[in] localTopology
   * @param[in] entryPoint
  */
  InstanceManager(HiCR::frontend::RPCEngine *rpcEngine, const HiCR::Topology localTopology, entryPoint_t entryPoint)
    : HiCR::InstanceManager(),
      _rpcEngine(rpcEngine),
      _localTopology(localTopology),
      _entryPoint(entryPoint)
  {}

  ~InstanceManager() = default;

  /**
   * Initialize the instance manager. Registers RPCs, stores available instances, detects the root instance, executes the entrypoint
  */
  __INLINE__ void initialize()
  {
    // Registering Topology gathering function
    auto gatherTopologiesExecutionUnit = HiCR::backend::pthreads::ComputeManager::createExecutionUnit([this](void *) { gatherTopologies(); });
    _rpcEngine->addRPCTarget(__CLOUDR_GATHER_TOPOLOGIES_RPC_NAME, gatherTopologiesExecutionUnit);

    // Registering launch function
    auto launchEntryPointExecutionUnit = HiCR::backend::pthreads::ComputeManager::createExecutionUnit([this](void *) { _entryPoint(); });
    _rpcEngine->addRPCTarget(__CLOUDR_LAUNCH_ENTRY_POINT_RPC_NAME, launchEntryPointExecutionUnit);

    // Registering relinquish instance function
    auto relinquishInstanceExecutionUnit = HiCR::backend::pthreads::ComputeManager::createExecutionUnit([this](void *) { relinquishInstance(); });
    _rpcEngine->addRPCTarget(__CLOUDR_RELINQUISH_INSTANCE_RPC_NAME, relinquishInstanceExecutionUnit);

    // Registering finalize function
    auto finalizeWorkerExecutionUnit = HiCR::backend::pthreads::ComputeManager::createExecutionUnit([this](void *) { finalizeWorker(); });
    _rpcEngine->addRPCTarget(__CLOUDR_FINALIZE_WORKER_RPC_NAME, finalizeWorkerExecutionUnit);

    // Registering exchange global memory slots RPC
    auto exchangeGlobalMemorySlotsExecutionUnit = HiCR::backend::pthreads::ComputeManager::createExecutionUnit([this](void *) { exchangeGlobalMemorySlotsRPC(); });
    _rpcEngine->addRPCTarget(__CLOUDR_EXCHANGE_GLOBAL_MEMORY_SLOTS_RPC_NAME, exchangeGlobalMemorySlotsExecutionUnit);

    // Registering global fence
    auto fenceExecutionUnit = HiCR::backend::pthreads::ComputeManager::createExecutionUnit([this](void *) { fenceRPC(); });
    _rpcEngine->addRPCTarget(__CLOUDR_FENCE_RPC_NAME, fenceExecutionUnit);

    // Creating instance objects from the initially found instances now
    HiCR::Instance::instanceId_t instanceIdCounter = 0;
    for (auto &instance : _rpcEngine->getInstanceManager()->getInstances())
    {
      // Only the current instance is the root one
      const bool isRoot = _rpcEngine->getInstanceManager()->getRootInstanceId() == instance->getId();

      // Creating new cloudr instance object (contains all the emulated information)
      auto newInstance = std::make_shared<HiCR::backend::cloudr::Instance>(instanceIdCounter, instance.get(), isRoot);

      // Adding new instance to the internal storage
      _cloudrInstances.push_back(newInstance);

      // If this is the current instance, set it now
      if (instance->getId() == _rpcEngine->getInstanceManager()->getCurrentInstance()->getId())
      {
        // Set as current instance
        setCurrentInstance(newInstance);

        // Assigning its topology
        newInstance->setTopology(_localTopology);
      }

      // If it's root, store its pointer
      if (isRoot)
      {
        // Store root instance pointer for later referencing
        _rootInstance = newInstance.get();

        // Adding instance to the collection of currently active instances
        addInstance(newInstance);
      }

      // If not root, add to the list of free instances (can be activated later)
      if (isRoot == false) _freeInstances.insert(newInstance.get());

      // Linking base instance id to the respective cloudr instance
      _baseIdsToCloudrInstanceMap[instance->getId()] = newInstance.get();

      // Increasing cloudr instance Id
      instanceIdCounter++;
    }
    // printf("[CloudR] Worker %lu finished.\n", _rpcEngine->getInstanceManager()->getCurrentInstance()->getId());

    ///// Now deploying

    // If I'm worker, all I need to do is listen for incoming RPCs
    if (_rpcEngine->getInstanceManager()->getCurrentInstance()->isRootInstance() == false)
    {
      while (_continueListening)
      {
        // printf("[CloudR] Worker %lu listening...\n", _rpcEngine->getInstanceManager()->getCurrentInstance()->getId());
        _rpcEngine->listen();
        // printf("[CloudR] Worker %lu back from listening...\n", _rpcEngine->getInstanceManager()->getCurrentInstance()->getId());
      }
    }
    else // If I am root, do the following instead
    {
      // Gather the topologies of all other instances
      for (auto &instance : _freeInstances)
      {
        // Requesting the root
        _rpcEngine->requestRPC(instance->getId(), __CLOUDR_GATHER_TOPOLOGIES_RPC_NAME);

        // Getting return value (topology)
        auto returnValue = _rpcEngine->getReturnValue();

        // Receiving raw serialized topology information from the worker
        std::string serializedTopology = (char *)returnValue->getPointer();

        // Parsing serialized raw topology into a json object
        auto topologyJson = nlohmann::json::parse(serializedTopology);

        // Freeing return value
        _rpcEngine->getMemoryManager()->freeLocalMemorySlot(returnValue);

        // Updating current instance's topology
        instance->setTopology(topologyJson);
      }

      // Then go straight to the entry point
      _entryPoint();

      // printf("[Root %lu] Exited entry point...\n", _rpcEngine->getInstanceManager()->getCurrentInstance()->getId());
    }
  }

  /**
   * This function is the RPC that a running instance receives when it is relinquished.
   * 
   * It does not terminate the worker, but simply confirms the instance is not running.
   */
  __INLINE__ void relinquishInstance()
  {
    // printf("Relinquishing...\n");

    // Returning confirmation that we are no longer running a function (idle)
    int returnOkMessage = 0;
    _rpcEngine->submitReturnValue((void *)&returnOkMessage, sizeof(returnOkMessage));
  }

   __INLINE__ void terminateInstanceImpl(const std::shared_ptr<HiCR::Instance> instance) override
   {
    // Requesting relinquish RPC execution on the requested instance
    _rpcEngine->requestRPC(instance->getId(), __CLOUDR_RELINQUISH_INSTANCE_RPC_NAME);

    // Getting return value. It's enough to know a value was returned to know it is idling
    const auto returnValue = _rpcEngine->getReturnValue();

    // Adding instance back to free instances
    _freeInstances.insert(_baseIdsToCloudrInstanceMap[instance->getId()]);
   }

  /**
   * Finalization procedure. Send rpc termination to all the non root instances
  */
  __INLINE__ void finalize() override
  {
    // printf("[Instance %lu] Finalizing CloudR...\n", _rpcEngine->getInstanceManager()->getCurrentInstance()->getId());

    // The following only be ran by the root rank, send an RPC to all others to finalize them
    if (_rpcEngine->getInstanceManager()->getCurrentInstance()->isRootInstance())
    {
      for (auto &instance : _cloudrInstances)
        if (instance->isRootInstance() == false) _rpcEngine->requestRPC(instance->getId(), __CLOUDR_FINALIZE_WORKER_RPC_NAME);
    }
  }

  /**
   * Abort execution with the specifies error code
   * 
   * @param[in] errorCode
  */
  __INLINE__ void abort(int errorCode) override { _rpcEngine->getInstanceManager()->abort(errorCode); }

  /**
   * Getter for root instance id
   * 
   * @return root instance id
  */
  [[nodiscard]] __INLINE__ HiCR::Instance::instanceId_t getRootInstanceId() const override { return _rootInstance->getId(); }

  /**
   * Getter for rpc engine 
   * 
   * @return rpc engine 
  */
  [[nodiscard]] __INLINE__ auto getRPCEngine() const { return _rpcEngine; }

  /**
   * Getter for free instances
   * 
   * @return free instances 
  */
  [[nodiscard]] __INLINE__ const auto &getFreeInstances() const { return _freeInstances; }

  private:

  /**
   * Response to gather topology rpc
  */
  __INLINE__ void gatherTopologies()
  {
    // Getting my current instance's topology
    auto topology = static_cast<HiCR::backend::cloudr::Instance *>(getCurrentInstance().get())->getTopology();

    // Serializing topology information
    const auto serializedTopology = topology.serialize().dump();

    // Returning serialized topology
    _rpcEngine->submitReturnValue((void *)serializedTopology.c_str(), serializedTopology.size() + 1);
  }

  __INLINE__ std::shared_ptr<HiCR::Instance> createInstanceImpl(const HiCR::InstanceTemplate instanceTemplate) override
  {
    // If no more free instances available, fail now
    // Commented out because we don't want to fail, simply return a nullptr
    // if (_freeInstances.empty()) HICR_THROW_LOGIC("Requested the creation of a new instances, but CloudR has ran out of free instances");

    // Creating instance object to return
    std::shared_ptr<HiCR::backend::cloudr::Instance> newInstance = nullptr;

    // Getting requested topology from the instance template
    const auto &topology = instanceTemplate.getTopology();

    // Iterating over free instances to get the first one that satisfies the request
    for (const auto &instance : _freeInstances)
      if (instance->isCompatible(topology))
      {
        // Assigning it as compatible instance
        newInstance = std::make_shared<HiCR::backend::cloudr::Instance>(*instance);

        // Erasing it from the list of free instances
        _freeInstances.erase(instance);

        // Stop looking into the others
        break;
      }

    // Commented out because we don't want to fail, simply return a nullptr
    // if (newInstance == nullptr)  HICR_THROW_LOGIC("Tried to create new instance but did not find any free instances that meet the required topology");

    // If successful, initialize the new instance
    if (newInstance != nullptr)
    {
      // Request the execution of the main driver function
      _rpcEngine->requestRPC(newInstance->getBaseInstance()->getId(), __CLOUDR_LAUNCH_ENTRY_POINT_RPC_NAME);
    }

    // Returning result. Nullptr, if no instance was created
    return newInstance;
  }

  __INLINE__ std::shared_ptr<HiCR::Instance> addInstanceImpl(HiCR::Instance::instanceId_t instanceId) override
  {
    HICR_THROW_LOGIC("The Host backend does not currently support the detection of new instances during runtime");
  }

  /**
   * Request exchange memory slots rpc
   * 
   * @param[in] tag the global memory slot tag to exchange
  */
  __INLINE__ void requestExchangeGlobalMemorySlots(HiCR::GlobalMemorySlot::tag_t tag)
  {
    // Asking free instances to run the exchange RPC
    for (const auto &instance : _freeInstances) _rpcEngine->requestRPC(instance->getId(), __CLOUDR_EXCHANGE_GLOBAL_MEMORY_SLOTS_RPC_NAME, tag);
  }

  /**
   * Request fence rpc
   * 
   * @param[in] tag the global memory slot tag to fence
  */
  __INLINE__ void requestFence(HiCR::GlobalMemorySlot::tag_t tag)
  {
    // Asking free instances to run the exchange RPC
    for (const auto &instance : _freeInstances) _rpcEngine->requestRPC(instance->getId(), __CLOUDR_FENCE_RPC_NAME, tag);
  }

  /**
   * Response to request topology rpc
  */
  __INLINE__ void requestTopology()
  {
    // Getting a pointer to the base instance who made the request
    auto baseRequesterInstance = _rpcEngine->getRPCRequester();

    // Getting base instance id
    const auto baseInstanceId = baseRequesterInstance->getId();

    // Getting cloudr instance from the base instance id
    const auto cloudrInstance = _baseIdsToCloudrInstanceMap.at(baseInstanceId);

    // Serializing topology information
    const auto serializedTopology = cloudrInstance->getTopology().serialize().dump();

    // Returning serialized topology
    _rpcEngine->submitReturnValue((void *)serializedTopology.c_str(), serializedTopology.size() + 1);
  }

  /**
   * Exit the worker rpc main loop and terminate execution
  */
  __INLINE__ void finalizeWorker()
  {
    // Do not continue listening
    // printf("[CloudR] Worker %lu running finalizeWorker() RPC.\n", _rpcEngine->getInstanceManager()->getCurrentInstance()->getId());
    _continueListening = false;
  }

  /**
   * rpc for exchange memory slots
  */
  __INLINE__ void exchangeGlobalMemorySlotsRPC()
  {
    const auto exchangeTag = _rpcEngine->getRPCArgument();
    _rpcEngine->getCommunicationManager()->exchangeGlobalMemorySlots(exchangeTag, {});
  }

  /**
   * rpc for fence 
  */
  __INLINE__ void fenceRPC()
  {
    const auto exchangeTag = _rpcEngine->getRPCArgument();
    _rpcEngine->getCommunicationManager()->fence(exchangeTag);
  }

  /// RPC engine
  HiCR::frontend::RPCEngine *const _rpcEngine;

  /// Storage for this instance's emulated topology
  const HiCR::Topology _localTopology;

  /// Storage for the main function to run when a new instance runs
  const entryPoint_t _entryPoint;

  // Pointer to the root instance
  HiCR::backend::cloudr::Instance *_rootInstance;

  // Flag to signal non-root instances to finish listening
  bool _continueListening = true;

  // Map that links the underlying instance ids with the cloudr instances
  std::map<HiCR::Instance::instanceId_t, HiCR::backend::cloudr::Instance *> _baseIdsToCloudrInstanceMap;

  /// Internal collection of cloudr instances
  std::vector<std::shared_ptr<HiCR::backend::cloudr::Instance>> _cloudrInstances;

  /// A collection of ready-to-use instances currently on standby
  std::set<HiCR::backend::cloudr::Instance*> _freeInstances;
}; // class CloudR

} // namespace HiCR::backend::cloudr