#pragma once

#include <hicr/core/communicationManager.hpp>
#include <hicr/core/instanceManager.hpp>
#include <hicr/core/memoryManager.hpp>
#include <hicr/core/computeManager.hpp>
#include <hicr/frontends/RPCEngine/RPCEngine.hpp>
#include <memory>

// Enabling topology managers (to discover the system's hardware) based on the selected backends during compilation

#ifdef _HICR_USE_ASCEND_BACKEND_
  #include <acl/acl.h>
  #include <hicr/backends/ascend/topologyManager.hpp>
#endif // _HICR_USE_ASCEND_BACKEND_

#ifdef _HICR_USE_HWLOC_BACKEND_
  #include <hwloc.h>
  #include <hicr/backends/hwloc/topologyManager.hpp>
#endif // _HICR_USE_HWLOC_BACKEND_

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
        #ifdef _HICR_USE_HWLOC_BACKEND_

        // Creating HWloc topology object
        auto topology = new hwloc_topology_t;
      
        // Reserving memory for hwloc
        hwloc_topology_init(topology);
      
        // Initializing HWLoc-based host (CPU) topology manager
        auto hwlocTopologyManager = std::make_unique<HiCR::backend::hwloc::TopologyManager>(topology);
      
        // Adding topology manager to the list
        _topologyManagers.push_back(std::move(hwlocTopologyManager));
      
      #endif // _HICR_USE_HWLOC_BACKEND_
      
      #ifdef _HICR_USE_ASCEND_BACKEND_
      
        // Initialize (Ascend's) ACL runtime
        aclError err = aclInit(NULL);
        if (err != ACL_SUCCESS) HICR_THROW_RUNTIME("Failed to initialize Ascend Computing Language. Error %d", err);
      
        // Initializing ascend topology manager
        auto ascendTopologyManager = std::make_unique<HiCR::backend::ascend::TopologyManager>();
      
        // Adding topology manager to the list
        _topologyManagers.push_back(std::move(ascendTopologyManager));
      
      #endif // _HICR_USE_ASCEND_BACKEND_
    };

    __INLINE__ HiCR::Topology getMachineTopology()
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

    virtual void abort() = 0;
    virtual void finalize() = 0;

    __INLINE__ bool isRootInstance() const {
        printf("Instance Id: %lu / root: %lu\n", _instanceManager->getCurrentInstance()->getId(),_instanceManager->getRootInstanceId());
         return _instanceManager->getCurrentInstance()->getId() == _instanceManager->getRootInstanceId(); }

    protected: 

    virtual void initializeManagers(int* pargc, char*** pargv) = 0;

    // Storage for the distributed engine's communication manager
    std::unique_ptr<HiCR::CommunicationManager> _communicationManager;

    // Storage for the distributed engine's instance manager
    std::unique_ptr<HiCR::InstanceManager> _instanceManager;

    // Storage for the distributed engine's memory manager
    std::unique_ptr<HiCR::MemoryManager> _memoryManager;

    // Storage for the distributed engine's topology managers
    std::vector<std::unique_ptr<HiCR::TopologyManager>> _topologyManagers;

}; // class Engine

} // namespace deployr