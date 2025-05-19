#pragma once

#include <hicr/core/communicationManager.hpp>
#include <hicr/core/instanceManager.hpp>
#include <hicr/core/memoryManager.hpp>
#include <hicr/core/computeManager.hpp>
#include <hicr/frontends/RPCEngine/RPCEngine.hpp>
#include <hicr/backends/pthreads/computeManager.hpp>
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

class Engine
{
    public:

    Engine() = default;
    virtual ~Engine() = default;

    __INLINE__ HiCR::InstanceManager::instanceList_t getHiCRInstances() const { return _instanceManager->getInstances(); }
    __INLINE__ void initialize(int* pargc, char*** pargv)
    {
        // initialize engine-specific managers
        initializeManagers(pargc, pargv);

        // Initializing topology managers, as configured

        // Creating HWloc topology object
        auto topology = new hwloc_topology_t;
      
        // Reserving memory for hwloc
        hwloc_topology_init(topology);
      
        // Initializing HWLoc-based host (CPU) topology manager
        auto hwlocTopologyManager = std::make_unique<HiCR::backend::hwloc::TopologyManager>(topology);
      
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
        auto RPCDevice = _topologyManagers.begin().operator*()->queryTopology().getDevices().begin().operator*();
        auto RPCMemorySpace = RPCDevice->getMemorySpaceList().begin().operator*();
        auto RPCComputeResource = RPCDevice->getComputeResourceList().begin().operator*();

        // Instantiating RPC engine
        _rpcEngine = std::make_unique<HiCR::frontend::RPCEngine>(*_communicationManager, *_instanceManager, *_memoryManager, *_computeManager, RPCMemorySpace, RPCComputeResource);

        // Initializing RPC engine
        _rpcEngine->initialize();
    };

    virtual void abort() = 0;
    virtual void finalize() = 0;

    __INLINE__ size_t getLocalInstanceIndex() const
    {
        const auto localHostInstanceId = _instanceManager->getCurrentInstance()->getId();

        const auto& instances = _instanceManager->getInstances(); 
        for (size_t i = 0; i < instances.size(); i++)
            if (instances[i]->getId() == localHostInstanceId)
                return i;
        
        return 0;
    }

    __INLINE__ bool isRootInstance() const { return _instanceManager->getCurrentInstance()->getId() == _instanceManager->getRootInstanceId(); }

    __INLINE__ HiCR::Instance& getRootInstance() const
    { 
      auto& instances = _instanceManager->getInstances();
      for (size_t i = 0; i < instances.size(); i++) if (instances[i]->isRootInstance()) return *(instances[i]);
      return *(instances[0]);
    }

    __INLINE__ size_t getRootInstanceIndex() const
    { 
      const auto& instances = _instanceManager->getInstances();
      for (size_t i = 0; i < instances.size(); i++) if (instances[i]->isRootInstance()) return i;
      return 0;
    }

    __INLINE__ void registerRPC(const std::string& RPCName, std::function<void()> fc)
    {
        // Registering RPC
        auto RPCExecutionUnit = HiCR::backend::pthreads::ComputeManager::createExecutionUnit([fc](void*){fc();});

        // Adding RPC
        _rpcEngine->addRPCTarget(RPCName, RPCExecutionUnit);
    }
    
    __INLINE__ void listenRPCs() { _rpcEngine->listen(); }

    __INLINE__ void launchRPC(const size_t instanceIndex, const std::string& RPCName)
    {
        auto& instances = _instanceManager->getInstances();
        auto& instance = instances[instanceIndex];
        _rpcEngine->requestRPC(*instance, RPCName);
    }

    __INLINE__ std::shared_ptr<Channel> createChannel(
        const Channel::channelId_t channelId,
        const std::string name,
        const std::vector<size_t> producerIdxs,
        const size_t consumerIdx,
        const size_t bufferSize,
        const size_t tokenSize)
        {
            auto channel = std::make_shared<Channel>(channelId);
            return channel;
        }

    __INLINE__ void submitRPCReturnValue(void* buffer, const size_t size) { _rpcEngine->submitReturnValue(buffer, size); }
    __INLINE__ std::shared_ptr<HiCR::LocalMemorySlot> getRPCReturnValue(HiCR::Instance &instance) const {  return _rpcEngine->getReturnValue(instance); }
    __INLINE__ void freeRPCReturnValue(std::shared_ptr<HiCR::LocalMemorySlot> returnValue) const {  _rpcEngine->getMemoryManager()->freeLocalMemorySlot(returnValue); }
    
    __INLINE__ nlohmann::json detectLocalTopology()
    {
        // Storage for the machine's topology
        HiCR::Topology topology;

        // Adding detected devices from all topology managers
        for (auto& tm : _topologyManagers)
        {
            // Getting the topology information from the topology manager
            const auto t = tm->queryTopology();

            // Merging its information to the worker topology object to send
            topology.merge(t);
        }

        // Returning merged topology
        return topology.serialize();
    }

    protected: 

    virtual void initializeManagers(int* pargc, char*** pargv) = 0;

    // Storage for the distributed engine's communication manager
    std::unique_ptr<HiCR::CommunicationManager> _communicationManager;

    // Storage for the distributed engine's instance manager
    std::unique_ptr<HiCR::InstanceManager> _instanceManager;

    // Storage for the distributed engine's memory manager
    std::unique_ptr<HiCR::MemoryManager> _memoryManager;

    // Storage for topology managers
    std::vector<std::unique_ptr<HiCR::TopologyManager>> _topologyManagers;

    // Storage for compute manager
    std::unique_ptr<HiCR::backend::pthreads::ComputeManager> _computeManager;

    // RPC engine
    std::unique_ptr<HiCR::frontend::RPCEngine> _rpcEngine;

}; // class Engine

} // namespace deployr