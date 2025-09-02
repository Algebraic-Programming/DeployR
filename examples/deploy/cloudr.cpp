#include <fstream>
#include <deployr/deployr.hpp>
#include <nlohmann_json/json.hpp>
#include <hicr/backends/cloudr/instanceManager.hpp>
#include <hicr/backends/cloudr/communicationManager.hpp>
#include <hicr/backends/mpi/communicationManager.hpp>
#include <hicr/backends/mpi/memoryManager.hpp>
#include <hicr/backends/mpi/instanceManager.hpp>
#include <hicr/backends/hwloc/topologyManager.hpp>
#include <deployr/deployr.hpp>
#include "deploy.hpp"

int main(int argc, char *argv[])
{
  // Instantiating base managers
  auto instanceManager         = HiCR::backend::mpi::InstanceManager::createDefault(&argc, &argv);
  auto communicationManager    = HiCR::backend::mpi::CommunicationManager(MPI_COMM_WORLD);
  auto memoryManager           = HiCR::backend::mpi::MemoryManager();
  auto computeManager          = HiCR::backend::pthreads::ComputeManager();

  // Reserving memory for hwloc
  hwloc_topology_t hwlocTopology;
  hwloc_topology_init(&hwlocTopology);

  // Initializing HWLoc-based host (CPU) topology manager
  auto hwlocTopologyManager = HiCR::backend::hwloc::TopologyManager(&hwlocTopology);

  // Finding the first memory space and compute resource to create our RPC engine
  const auto& topology           = hwlocTopologyManager.queryTopology();
  const auto& firstDevice        = topology.getDevices().begin().operator*();
  const auto& RPCMemorySpace     = firstDevice->getMemorySpaceList().begin().operator*();
  const auto& RPCComputeResource = firstDevice->getComputeResourceList().begin().operator*();
  
  // Instantiating RPC engine
  HiCR::frontend::RPCEngine rpcEngine(communicationManager, *instanceManager, memoryManager, computeManager, RPCMemorySpace, RPCComputeResource);

  // Initializing RPC engine
  rpcEngine.initialize();

  // Instantiating CloudR
  HiCR::backend::cloudr::InstanceManager cloudrInstanceManager(&rpcEngine, topology, [&]()
  {
    // Creating deployr object
    deployr::DeployR deployr(&cloudrInstanceManager, &rpcEngine, topology);

    // Calling main algorithm driver
    deploy(deployr, {});
  });

  // Initializing CloudR
  cloudrInstanceManager.initialize();

  // Finalizing cloudR
  cloudrInstanceManager.finalize();

  // Finalizing base instance manager
  instanceManager->finalize();
}