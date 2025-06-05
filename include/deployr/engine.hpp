#pragma once

#include <hicr/core/communicationManager.hpp>
#include <hicr/core/instanceManager.hpp>
#include <hicr/core/memoryManager.hpp>
#include <hicr/core/computeManager.hpp>
#include <hicr/frontends/RPCEngine/RPCEngine.hpp>
#include <hicr/backends/pthreads/computeManager.hpp>
#include <hicr/frontends/channel/variableSize/mpsc/locking/consumer.hpp>
#include <hicr/frontends/channel/variableSize/mpsc/locking/producer.hpp>
#include "channel.hpp"
#include <memory>
#include <set>

//// Enabling topology managers (to discover the system's hardware) based on the selected backends during compilation

// The HWLoc topology manager is mandatory

#include <hwloc.h>
#include <hicr/backends/hwloc/topologyManager.hpp>

// Optional topology managers follow

#ifdef _HICR_USE_ASCEND_BACKEND_
  #include <acl/acl.h>
  #include <hicr/backends/ascend/topologyManager.hpp>
#endif // _HICR_USE_ASCEND_BACKEND_

namespace deployr
{

/**
 * HiCR Management Engine for DeployR
 * 
 * Instantiates device-specific HiCR backends and resolves the detection of the system topology and channel creation.
 */
class Engine
{
  public:

  Engine()          = default;
  virtual ~Engine() = default;

  /**
   * Gets a collection, ordered by index, of all the detected / created HiCR instances
   * 
   * @return An array with the detected / created HiCR instances
   */
  [[nodiscard]] __INLINE__ HiCR::InstanceManager::instanceList_t getHiCRInstances() const { return _instanceManager->getInstances(); }

  /**
   *  Initializes the internal HiCR managers required for DeployR and those chosen by the user. 
   *  It instantiates and initializes the RPC engine for sending functions across instances.
   * 
   *  @param[in] pargc A pointer to the argc value given in main. Its value is initialized at this point. Using it before will result in undefined behavior.
   *  @param[in] pargv A pointer to the argv value given in main. Its value is initialized at this point. Using it before will result in undefined behavior.
   */
  __INLINE__ void initialize(int *pargc, char ***pargv)
  {
    // initialize engine-specific managers
    initializeManagers(pargc, pargv);

    // Reserving memory for hwloc
    hwloc_topology_init(&_hwlocTopology);

    // Initializing HWLoc-based host (CPU) topology manager
    auto hwlocTopologyManager = std::make_unique<HiCR::backend::hwloc::TopologyManager>(&_hwlocTopology);

    // Adding topology manager to the list
    _topologyManagers.push_back(std::move(hwlocTopologyManager));

#ifdef _HICR_USE_ASCEND_BACKEND_

    // Initialize (Ascend's) ACL runtime
    aclError err = aclInit(NULL);
    if (err != ACL_SUCCESS) HICR_THROW_RUNTIME("Failed to initialize Ascend Computing Language. Error %d", err);

    // Initializing ascend topology manager
    auto ascendTopologyManager = std::make_unique<HiCR::backend::ascend::TopologyManager>();

    // Adding topology manager to the list
    _topologyManagers.push_back(std::move(ascendTopologyManager));

#endif // _HICR_USE_ASCEND_BACKEND_

    // Initializing RPC-related managers
    _computeManager = std::make_unique<HiCR::backend::pthreads::ComputeManager>();

    // Finding the first memory space and compute resource to create our RPC engine
    _firstDevice            = _topologyManagers.begin().operator*()->queryTopology().getDevices().begin().operator*();
    auto RPCMemorySpace     = _firstDevice->getMemorySpaceList().begin().operator*();
    auto RPCComputeResource = _firstDevice->getComputeResourceList().begin().operator*();

    // Instantiating RPC engine
    _rpcEngine = std::make_unique<HiCR::frontend::RPCEngine>(*_communicationManager, *_instanceManager, *_memoryManager, *_computeManager, RPCMemorySpace, RPCComputeResource);

    // Initializing RPC engine
    _rpcEngine->initialize();
  };

  /**
   * Function to fatally abort execution in all instances. 
   * It can be called by any one instance.
   * It delegates the implementation to the configured backends
   */
  virtual void abort() = 0;

  /**
   * Function to normally finalize execution.
   * It must be called by all instances.
   * It delegates the implementation to the configured backends
   */
  virtual void finalize() = 0;

  /**
    * Gets the index within the HiCR instance list corresponding to this local instance
    * 
    * @return The index corresponding to this instance
    */
  [[nodiscard]] __INLINE__ size_t getLocalInstanceIndex() const
  {
    const auto localHostInstanceId = _instanceManager->getCurrentInstance()->getId();

    const auto &instances = _instanceManager->getInstances();
    for (size_t i = 0; i < instances.size(); i++)
      if (instances[i]->getId() == localHostInstanceId) return i;

    return 0;
  }

  /**
    * Indicates whether the local instance is the HiCR root instance
    * 
    * @return true, if this is the root instance; false, otherwise.
    */
  [[nodiscard]] __INLINE__ bool isRootInstance() const { return _instanceManager->getCurrentInstance()->getId() == _instanceManager->getRootInstanceId(); }

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
    * Executes one pending RPC request or suspends the current instance in wait for the next arriving RPC
    * 
    * @note This function will not return until and unless an RPC has executed
    */
  __INLINE__ void listenRPCs() { _rpcEngine->listen(); }

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
    _rpcEngine->requestRPC(*instance, RPCName);
  }

#define SIZES_BUFFER_KEY 0
#define CONSUMER_COORDINATION_BUFFER_FOR_SIZES_KEY 3
#define CONSUMER_COORDINATION_BUFFER_FOR_PAYLOADS_KEY 4
#define CONSUMER_PAYLOAD_KEY 5
#define _CHANNEL_CREATION_ERROR 1

  /**
    * Function to create a variable-sized token locking channel between N producers and 1 consumer
    * 
    * @note This is a collective operation. All instances must participate in this call, even if they don't play a producer or consumer role
    * 
    * @param[in] channelTag The unique identifier for the channel. This tag should be unique for each channel
    * @param[in] channelName The name of the channel. This will be the identifier used to retrieve the channel
    * @param[in] producerIdxs Indexes of the instances within the HiCR instance list to serve as producers
    * @param[in] consumerIdx Index of the instance within the HiCR instance list to serve as consumer
    * @param[in] bufferCapacity The number of tokens that can be simultaneously held in the channel's buffer
    * @param[in] bufferSize The size (bytes) of the buffer.abort
    * @return A shared pointer of the newly created channel
    */
  __INLINE__ std::shared_ptr<Channel> createChannel(const size_t              channelTag,
                                                    const std::string         channelName,
                                                    const std::vector<size_t> producerIdxs,
                                                    const size_t              consumerIdx,
                                                    const size_t              bufferCapacity,
                                                    const size_t              bufferSize)
  {
    // printf("Creating channel %lu '%s', producer count: %lu (first: %lu), consumer %lu, bufferCapacity: %lu, bufferSize: %lu\n", channelTag, name.c_str(), producerIdxs.size(), producerIdxs.size() > 0 ? producerIdxs[0] : 0, consumerIdx, bufferCapacity, bufferSize);

    // Interfaces for the channel
    std::shared_ptr<HiCR::channel::variableSize::MPSC::locking::Consumer> consumerInterface = nullptr;
    std::shared_ptr<HiCR::channel::variableSize::MPSC::locking::Producer> producerInterface = nullptr;

    // Getting my local instance index
    auto localInstanceIndex = getLocalInstanceIndex();

    // Checking if I am consumer
    bool isConsumer = consumerIdx == getLocalInstanceIndex();

    // Checking if I am producer
    bool isProducer = false;
    for (const auto producerIdx : producerIdxs)
      if (producerIdx == localInstanceIndex)
      {
        isProducer = true;
        break;
      }

    // Finding the first memory space to create our channels
    auto bufferMemorySpace = _firstDevice->getMemorySpaceList().begin().operator*();

    // If I am consumer, create the consumer interface for the channel
    if (isConsumer == true && isProducer == false)
    {
      // Getting required buffer sizes
      auto sizesBufferSize = HiCR::channel::variableSize::Base::getTokenBufferSize(sizeof(size_t), bufferCapacity);

      // Allocating sizes buffer as a local memory slot
      auto sizesBufferSlot = _memoryManager->allocateLocalMemorySlot(bufferMemorySpace, sizesBufferSize);

      // Allocating payload buffer as a local memory slot
      auto payloadBufferSlot = _memoryManager->allocateLocalMemorySlot(bufferMemorySpace, bufferSize);

      // Getting required buffer size
      auto coordinationBufferSize = HiCR::channel::variableSize::Base::getCoordinationBufferSize();

      // Allocating coordination buffer for internal message size metadata
      auto coordinationBufferForSizes = _memoryManager->allocateLocalMemorySlot(bufferMemorySpace, coordinationBufferSize);

      // Allocating coordination buffer for internal payload metadata
      auto coordinationBufferForPayloads = _memoryManager->allocateLocalMemorySlot(bufferMemorySpace, coordinationBufferSize);

      // Initializing coordination buffer (sets to zero the counters)
      HiCR::channel::variableSize::Base::initializeCoordinationBuffer(coordinationBufferForSizes);

      HiCR::channel::variableSize::Base::initializeCoordinationBuffer(coordinationBufferForPayloads);

      // Exchanging local memory slots to become global for them to be used by the remote end
      _communicationManager->exchangeGlobalMemorySlots(channelTag,
                                                       {{SIZES_BUFFER_KEY, sizesBufferSlot},
                                                        {CONSUMER_COORDINATION_BUFFER_FOR_SIZES_KEY, coordinationBufferForSizes},
                                                        {CONSUMER_COORDINATION_BUFFER_FOR_PAYLOADS_KEY, coordinationBufferForPayloads},
                                                        {CONSUMER_PAYLOAD_KEY, payloadBufferSlot}});

      // Synchronizing so that all actors have finished registering their global memory slots
      _communicationManager->fence(channelTag);

      // Obtaining the globally exchanged memory slots
      auto globalSizesBufferSlot                 = _communicationManager->getGlobalMemorySlot(channelTag, SIZES_BUFFER_KEY);
      auto consumerCoordinationBufferForSizes    = _communicationManager->getGlobalMemorySlot(channelTag, CONSUMER_COORDINATION_BUFFER_FOR_SIZES_KEY);
      auto consumerCoordinationBufferForPayloads = _communicationManager->getGlobalMemorySlot(channelTag, CONSUMER_COORDINATION_BUFFER_FOR_PAYLOADS_KEY);
      auto globalPayloadBuffer                   = _communicationManager->getGlobalMemorySlot(channelTag, CONSUMER_PAYLOAD_KEY);

      // Creating producer and consumer channels
      consumerInterface = std::make_shared<HiCR::channel::variableSize::MPSC::locking::Consumer>(*_communicationManager,
                                                                                                 globalPayloadBuffer,   /* payloadBuffer */
                                                                                                 globalSizesBufferSlot, /* tokenSizeBuffer */
                                                                                                 coordinationBufferForSizes,
                                                                                                 coordinationBufferForPayloads,
                                                                                                 consumerCoordinationBufferForSizes,
                                                                                                 consumerCoordinationBufferForPayloads,
                                                                                                 bufferSize,
                                                                                                 bufferCapacity);
    }

    // If I am producer, create the producer interface for the channel
    if (isProducer == true && isConsumer == false)
    {
      // Getting required buffer size
      auto coordinationBufferSize = HiCR::channel::variableSize::Base::getCoordinationBufferSize();

      // Allocating sizes buffer as a local memory slot
      auto coordinationBufferForSizes = _memoryManager->allocateLocalMemorySlot(bufferMemorySpace, coordinationBufferSize);

      auto coordinationBufferForPayloads = _memoryManager->allocateLocalMemorySlot(bufferMemorySpace, coordinationBufferSize);

      auto sizeInfoBuffer = _memoryManager->allocateLocalMemorySlot(bufferMemorySpace, sizeof(size_t));

      // Initializing coordination buffers for message sizes and payloads (sets to zero the counters)
      HiCR::channel::variableSize::Base::initializeCoordinationBuffer(coordinationBufferForSizes);
      HiCR::channel::variableSize::Base::initializeCoordinationBuffer(coordinationBufferForPayloads);

      // Exchanging local memory slots to become global for them to be used by the remote end
      _communicationManager->exchangeGlobalMemorySlots(channelTag, {});

      // Synchronizing so that all actors have finished registering their global memory slots
      _communicationManager->fence(channelTag);

      // Obtaining the globally exchanged memory slots
      auto sizesBuffer                           = _communicationManager->getGlobalMemorySlot(channelTag, SIZES_BUFFER_KEY);
      auto consumerCoordinationBufferForSizes    = _communicationManager->getGlobalMemorySlot(channelTag, CONSUMER_COORDINATION_BUFFER_FOR_SIZES_KEY);
      auto consumerCoordinationBufferForPayloads = _communicationManager->getGlobalMemorySlot(channelTag, CONSUMER_COORDINATION_BUFFER_FOR_PAYLOADS_KEY);
      auto payloadBuffer                         = _communicationManager->getGlobalMemorySlot(channelTag, CONSUMER_PAYLOAD_KEY);

      // Creating producer and consumer channels
      producerInterface = std::make_shared<HiCR::channel::variableSize::MPSC::locking::Producer>(*_communicationManager,
                                                                                                 sizeInfoBuffer,
                                                                                                 payloadBuffer,
                                                                                                 sizesBuffer,
                                                                                                 coordinationBufferForSizes,
                                                                                                 coordinationBufferForPayloads,
                                                                                                 consumerCoordinationBufferForSizes,
                                                                                                 consumerCoordinationBufferForPayloads,
                                                                                                 bufferSize,
                                                                                                 sizeof(char),
                                                                                                 bufferCapacity);
    }

    // If I am not involved in this channel (neither consumer or producer, simply participate in the exchange)
    if (isConsumer == false && isProducer == false)
    {
      // Exchanging local memory slots to become global for them to be used by the remote end
      _communicationManager->exchangeGlobalMemorySlots(channelTag, {});

      // Synchronizing so that all actors have finished registering their global memory slots
      _communicationManager->fence(channelTag);
    }

    return std::make_shared<Channel>(channelName, _memoryManager.get(), bufferMemorySpace, consumerInterface, producerInterface);
  }

  /**
    * When executing an RPC, this function provides a buffer to serve as return value
    * The buffer will be sent to the RPC caller instance
    * 
    * @param[in] buffer The buffer containing the data to return
    * @param[in] size The size of the return value buffer
    */
  __INLINE__ void submitRPCReturnValue(void *buffer, const size_t size) { _rpcEngine->submitReturnValue(buffer, size); }

  /**
    * For the RPC caller, this function retrieves the callee's return value
    * 
    * @note This function will block the instance until the value is received
    * 
    * @param[in] instance HiCR instance object to listen for the return value for
    * @return A HiCR local memory slot containing the return value
    */
  [[nodiscard]] __INLINE__ std::shared_ptr<HiCR::LocalMemorySlot> getRPCReturnValue(HiCR::Instance &instance) const { return _rpcEngine->getReturnValue(instance); }

  /**
    * Releases the memory of a received return value
    * 
    * @param[in] returnValue The return value object to free
    */
  __INLINE__ void freeRPCReturnValue(std::shared_ptr<HiCR::LocalMemorySlot> returnValue) const { _rpcEngine->getMemoryManager()->freeLocalMemorySlot(returnValue); }

  /**
    * Detects the local hardware and network topology of the local host
    * 
    * @return A JSON-encoded HiCR topology containing all the detected devices
    */
  __INLINE__ nlohmann::json detectLocalTopology()
  {
    // Storage for the machine's topology
    HiCR::Topology topology;

    // Adding detected devices from all topology managers
    for (auto &tm : _topologyManagers)
    {
      // Getting the topology information from the topology manager
      const auto t = tm->queryTopology();

      // Merging its information to the worker topology object to send
      topology.merge(t);
    }

    // Returning merged topology
    return topology.serialize();
  }

  __INLINE__ HiCR::frontend::RPCEngine* getRPCEngine() { return _rpcEngine.get(); }

  protected:

  /**
    * Initializes the HiCR managers for topology, instance, memory, and communication
    * 
    *  @param[in] pargc A pointer to the argc value given in main. Its value is initialized at this point. Using it before will result in undefined behavior.
    *  @param[in] pargv A pointer to the argv value given in main. Its value is initialized at this point. Using it before will result in undefined behavior.
    */
  virtual void initializeManagers(int *pargc, char ***pargv) = 0;

  /// Storage for the distributed engine's communication manager
  std::unique_ptr<HiCR::CommunicationManager> _communicationManager;

  /// Storage for the distributed engine's instance manager
  std::unique_ptr<HiCR::InstanceManager> _instanceManager;

  /// Storage for the distributed engine's memory manager
  std::unique_ptr<HiCR::MemoryManager> _memoryManager;

  /// Storage for topology managers
  std::vector<std::unique_ptr<HiCR::TopologyManager>> _topologyManagers;

  /// Storage for compute manager
  std::unique_ptr<HiCR::backend::pthreads::ComputeManager> _computeManager;

  /// RPC engine
  std::unique_ptr<HiCR::frontend::RPCEngine> _rpcEngine;

  /// First device to use as buffer source
  std::shared_ptr<HiCR::Device> _firstDevice;

  /// Hwloc topology object
  hwloc_topology_t _hwlocTopology;

}; // class Engine

} // namespace deployr