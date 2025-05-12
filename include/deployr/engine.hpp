#pragma once

#include <hicr/core/communicationManager.hpp>
#include <hicr/core/instanceManager.hpp>
#include <hicr/core/memoryManager.hpp>
#include <hicr/core/computeManager.hpp>
#include <hicr/frontends/RPCEngine/RPCEngine.hpp>
#include <hicr/backends/pthreads/computeManager.hpp>
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

#define __DEPLOYR_TOPOLOGY_RPC_NAME "[DeployR] Get Topology"

namespace deployr 
{

class Engine
{
    public:

    Engine() = default;
    virtual ~Engine() = default;

    __INLINE__ size_t getInstanceCount() const { return _instanceManager->getInstances().size(); }
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

        // Getting local topology
        _localTopology = detectLocalTopology();

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

        // Registering topology exchanging RPC
        auto getTopologyExecutionunit = HiCR::backend::pthreads::ComputeManager::createExecutionUnit([this](void*)
        {
            // Serializing
            const auto serializedTopology = _localTopology.serialize().dump();

            // Returning serialized topology
            _rpcEngine->submitReturnValue((void*)serializedTopology.c_str(), serializedTopology.size());
        });
        _rpcEngine->addRPCTarget(__DEPLOYR_TOPOLOGY_RPC_NAME, getTopologyExecutionunit);

        // Gathering global topology into the root instance
        _globalTopology = gatherGlobalTopology();
    };

    __INLINE__ const HiCR::Topology& getLocalTopology() const { return _localTopology; }
    __INLINE__ const std::vector<HiCR::Topology>& getGlobalTopology() const { return _globalTopology; }

    virtual void abort() = 0;
    virtual void finalize() = 0;

    __INLINE__ bool isRootInstance() const { return _instanceManager->getCurrentInstance()->getId() == _instanceManager->getRootInstanceId(); }
    __INLINE__ size_t getRootInstanceIndex() const
    { 
      const auto& instances = _instanceManager->getInstances();
      for (size_t i = 0; i < instances.size(); i++) if (instances[i]->isRootInstance()) return i;
      return 0;
    }

    __INLINE__ void registerRPC(const std::string& RPCName, std::function<void()> fc)
    {
        // Registering RPC
        auto RPCExecutionUnit = HiCR::backend::pthreads::ComputeManager::createExecutionUnit([&](void*){fc();});

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

    // Storage for the local system topology
    HiCR::Topology _localTopology;

    // Storage for the global system topology
    std::vector<HiCR::Topology> _globalTopology;

    private:

    __INLINE__ HiCR::Topology detectLocalTopology()
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
        return topology;
    }

    __INLINE__ std::vector<HiCR::Topology> gatherGlobalTopology()
    {
        // Storage
        std::vector<HiCR::Topology> globalTopology;

        // If I am not root, listen for the incoming RPC and return an empty topology
        if (isRootInstance() == false) _rpcEngine->listen(); 

        // If I am root, request topology from all instances
        else
         for (const auto& instance : _instanceManager->getInstances())
            // If its the root instance (me), just push my local topology
            if (instance->isRootInstance() == true) globalTopology.push_back(_localTopology);
            // If not, it's another instance: send RPC and deserialize return value
            else
            {
                // Requessting RPC from the remote instance
                _rpcEngine->requestRPC(*instance, __DEPLOYR_TOPOLOGY_RPC_NAME);

                // Getting return value as a memory slot
                auto returnValue = _rpcEngine->getReturnValue(*instance);

                // Receiving raw serialized topology information from the worker
                std::string serializedTopology = (char *)returnValue->getPointer();

                // Parsing serialized raw topology into a json object
                auto topologyJson = nlohmann::json::parse(serializedTopology);

                // Freeing return value
                _rpcEngine->getMemoryManager()->freeLocalMemorySlot(returnValue);

                // HiCR topology object to obtain
                HiCR::Topology topology;

                // Obtaining the topology from the serialized object
                topology.merge(HiCR::backend::hwloc::TopologyManager::deserializeTopology(topologyJson));

                #ifdef _HICR_USE_ASCEND_BACKEND_
                topology.merge(HiCR::backend::ascend::TopologyManager::deserializeTopology(topologyJson));
                #endif // _HICR_USE_ASCEND_BACKEND_

                // Pushing topology into the vector
                globalTopology.push_back(topology);
            }
        
        // Return global topology
        return globalTopology;
    }

}; // class Engine

} // namespace deployr