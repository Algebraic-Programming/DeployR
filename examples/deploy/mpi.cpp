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

  // Making sure we instantiated 3 instances, which is all we need for this example
  if (instanceManager->getInstances().size() != 3) 
  {
    fprintf(stderr, "Error: this example requires three instances to run.\n");
    instanceManager->abort(-1);
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

  // Gathering instances to run the example with
  std::vector<HiCR::Instance*> instances;
  for (const auto& instance : instanceManager->getInstances()) instances.push_back(instance.get());

  // Initialize RPC engine
  rpcEngine.initialize();

  // Creating deployr object
  deployr::DeployR deployr(instanceManager.get(), &rpcEngine, topology);

  // Initializing deployr object
  deployr.initialize();

  // Getting the topology of the other MPI processes
  std::vector<HiCR::Instance::instanceId_t> instanceIds;
  for (const auto& instance : instanceManager->getInstances()) instanceIds.push_back(instance->getId());
  const auto globalTopology = deployr.gatherGlobalTopology(instanceManager->getRootInstanceId(), instanceIds);
  
  // Creating deployment object
  deployr::Deployment deployment;

  // Gathering deployment information from json file. This only needs to be done by the deployment coordinator (root, in this case)
  if (instanceManager->getCurrentInstance()->isRootInstance())
  {
    // Checking arguments
    if (argc != 2)
    {   
      fprintf(stderr, "Error: You need to pass a deployment.json file as parameter.\n");
      instanceManager->abort(-1);
    }

    // Reading deployment file
    std::string deploymentFilePath = std::string(argv[1]);

    // Parsing request file contents to a JSON object
    std::ifstream ifs(deploymentFilePath);
    auto  deploymentJs = nlohmann::json::parse(ifs);

    // Getting requested topologies from the json file
    std::vector<HiCR::Topology> requestedTopologies;
    for (const auto& runner : deploymentJs["Runners"]) requestedTopologies.push_back(HiCR::Topology(runner["Topology"]));

    // Determine best pairing between the detected instances
    const auto matching = deployr::DeployR::doBipartiteMatching(requestedTopologies, globalTopology);

    // Check matching
    if (matching.size() != requestedTopologies.size())
    {   
      fprintf(stderr, "Error: The provided instances do not have the sufficient hardware resources to run this job.\n");
      instanceManager->abort(-1);
    }

    // Creating the runner objects
    for (size_t i = 0; i < deploymentJs["Runners"].size(); i++)
     deployment.addRunner(deployr::Runner(i, deploymentJs["Runners"][i]["Function"].get<std::string>(), matching[i]));
  }

  // Deploying
  deploy(deployr, deployment, instanceManager->getRootInstanceId());

  // Finalizing instance manager
  instanceManager->finalize();
}