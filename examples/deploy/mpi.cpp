#include <deployr/deployr.hpp>
#include <nlohmann_json/json.hpp>
#include <fstream>
#include <hicr/backends/mpi/instanceManager.hpp>
#include <hicr/backends/mpi/communicationManager.hpp>
#include <hicr/backends/mpi/memoryManager.hpp>
#include <hicr/frontends/RPCEngine/RPCEngine.hpp>
#include <hicr/backends/hwloc/topologyManager.hpp>
#include "deploy.hpp"

int main(int argc, char *argv[])
{
  // Getting MPI managers
  auto instanceManager      = HiCR::backend::mpi::InstanceManager::createDefault(&argc, &argv);
  auto communicationManager = std::make_shared<HiCR::backend::mpi::CommunicationManager>();
  auto memoryManager        = std::make_shared<HiCR::backend::mpi::MemoryManager>();

  // Checking if I'm root
  bool isRoot = instanceManager->getCurrentInstance()->isRootInstance();

  // Configuration for deployR. Only needs to be provided by the root instance
  nlohmann::json deployrConfigJs;

  // Parsing arguments (only if I'm root)
  if (isRoot)
  {
    // Checking arguments
    if (argc != 2)
    {
      fprintf(stderr, "Error: Must provide a DeployR JSON configuration file\n");
      instanceManager->abort(-1);
      return -1;
    }

      // Getting DeployR configuration file path from arguments
      auto deployrConfigFilePath = argv[1];

      // Parsing DeployR configuration file contents to a JSON object
      std::ifstream ifs(deployrConfigFilePath);
      deployrConfigJs = nlohmann::json::parse(ifs);
  }

  // Creating HWloc topology object
  hwloc_topology_t hwlocTopology;

  // Reserving memory for hwloc
  hwloc_topology_init(&hwlocTopology);

  // Initializing host (CPU) topology manager
  HiCR::backend::hwloc::TopologyManager tm(&hwlocTopology);

  // Gathering topology from the topology manager
  const auto topology = tm.queryTopology();

  // Selecting first device
  auto device = *topology.getDevices().begin();

  // Getting memory space list from device
  auto memSpaces = device->getMemorySpaceList();

  // Grabbing first memory space for buffering
  auto bufferMemorySpace = *memSpaces.begin();

  // Now getting compute resource list from device
  auto computeResources = device->getComputeResourceList();

  // Grabbing first compute resource for computing incoming RPCs
  auto computeResource = *computeResources.begin();

  // Creating compute manager (responsible for executing the RPCs)
  HiCR::backend::pthreads::ComputeManager computeManager;

  // Creating RPC engine instance
  HiCR::frontend::RPCEngine rpcEngine(*communicationManager, *instanceManager, *memoryManager, computeManager, bufferMemorySpace, computeResource);

  // Initialize RPC engine
  rpcEngine.initialize();

  // Creating deployr object
  deployr::DeployR deployr(instanceManager.get(), &rpcEngine, topology);

  // Calling main algorithm driver
  deploy(deployr, deployrConfigJs);

  // Finalizing instance manager
  instanceManager->finalize();
}