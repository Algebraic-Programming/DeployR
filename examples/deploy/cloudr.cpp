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
  auto instanceManager      = HiCR::backend::mpi::InstanceManager::createDefault(&argc, &argv);
  auto communicationManager = HiCR::backend::mpi::CommunicationManager(MPI_COMM_WORLD);
  auto memoryManager        = HiCR::backend::mpi::MemoryManager();
  auto computeManager       = HiCR::backend::pthreads::ComputeManager();

  // Getting my base instance id and index
  const auto baseInstanceId  = instanceManager->getCurrentInstance()->getId();
  uint64_t   baseInstanceIdx = 0;
  for (size_t i = 0; i < instanceManager->getInstances().size(); i++)
    if (instanceManager->getInstances()[i]->getId() == baseInstanceId) baseInstanceIdx = i;

  // Checking for parameters
  if (argc != 3)
  {
    fprintf(stderr, "Error: You need to pass a deployment.json and a cloudr.json file as parameters.\n");
    instanceManager->finalize();
    return -1;
  }

  // Reading cloudr config file
  std::string cloudrConfigFilePath = std::string(argv[2]);

  // Parsing deployment file contents to a JSON object
  std::ifstream ifs(cloudrConfigFilePath);
  auto          cloudrConfigJs = nlohmann::json::parse(ifs);

  // Make sure we're running the number of base instances as emulated cloudr instances
  if (instanceManager->getInstances().size() != cloudrConfigJs["Topologies"].size())
  {
    fprintf(stderr,
            "Error: The number of requested cloudr instances (%lu) is different than the number of instances provided (%lu)\n",
            cloudrConfigJs["Topologies"].size(),
            instanceManager->getInstances().size());
    instanceManager->finalize();
    return -1;
  }

  // Getting my emulated topology from the cloudr configuration file
  HiCR::Topology emulatedTopology(cloudrConfigJs["Topologies"][baseInstanceIdx]);

  // Reserving memory for hwloc
  hwloc_topology_t hwlocTopology;
  hwloc_topology_init(&hwlocTopology);

  // Initializing HWLoc-based host (CPU) topology manager
  auto hwlocTopologyManager = HiCR::backend::hwloc::TopologyManager(&hwlocTopology);

  // Finding the first memory space and compute resource to create our RPC engine
  const auto &topology           = hwlocTopologyManager.queryTopology();
  const auto &firstDevice        = topology.getDevices().begin().operator*();
  const auto &RPCMemorySpace     = firstDevice->getMemorySpaceList().begin().operator*();
  const auto &RPCComputeResource = firstDevice->getComputeResourceList().begin().operator*();

  // Instantiating RPC engine
  HiCR::frontend::RPCEngine rpcEngine(communicationManager, *instanceManager, memoryManager, computeManager, RPCMemorySpace, RPCComputeResource);

  // Initializing RPC engine
  rpcEngine.initialize();

  // Creating deployment object
  deployr::Deployment deployment;

  // Instantiating CloudR
  HiCR::backend::cloudr::InstanceManager cloudrInstanceManager(&rpcEngine, emulatedTopology, [&]() {
    // Getting our current cloudr instance
    const auto &currentInstance = cloudrInstanceManager.getCurrentInstance();

    // Getting our instance's emulated topology
    const auto &emulatedTopology = dynamic_pointer_cast<HiCR::backend::cloudr::Instance>(currentInstance)->getTopology();

    // Creating deployr object
    deployr::DeployR deployr(&cloudrInstanceManager, &rpcEngine, emulatedTopology);

    // Calling main algorithm driver
    deploy(deployr, deployment, cloudrInstanceManager.getRootInstanceId());
  });

  // Initializing CloudR -- this is a bifurcation point. Only the root instance advances now
  cloudrInstanceManager.initialize();

  // For cleanup purposes, remember the newly created instances
  std::vector<std::shared_ptr<HiCR::Instance>> newInstances;

  // Gathering deployment information from json file. This only needs to be done by the deployment coordinator (root, in this case)
  if (instanceManager->getCurrentInstance()->isRootInstance())
  {
    // Reading deployment file
    std::string deploymentFilePath = std::string(argv[1]);

    // Parsing deployment file contents to a JSON object
    std::ifstream ifs(deploymentFilePath);
    auto          deploymentJs = nlohmann::json::parse(ifs);

    // Getting requested topologies from the json file
    for (size_t i = 0; i < deploymentJs["Runners"].size(); i++)
    {
      // Getting runner
      const auto &runner = deploymentJs["Runners"][i];

      // Assigning runner topology
      const auto runnerTopology = HiCR::Topology(runner["Topology"]);

      // Asking cloudr to create new instances based on the topology requirement
      const auto instanceTemplate = cloudrInstanceManager.createInstanceTemplate(runnerTopology);
      auto       instance         = cloudrInstanceManager.createInstance(*instanceTemplate);

      // Adding new instances to list of newly created instances
      newInstances.push_back(instance);

      // Sanity check
      if (instance == nullptr)
      {
        fprintf(stderr, "Error: Could not create instance with required topology: %s\n", runnerTopology.serialize().dump(2).c_str());
        instanceManager->abort(-1);
      }

      // Creating runner
      deployment.addRunner(deployr::Runner(i, deploymentJs["Runners"][i]["Function"].get<std::string>(), instance->getId()));
    }

    // Creating deployr object
    deployr::DeployR deployr(&cloudrInstanceManager, &rpcEngine, topology);

    // Calling main algorithm driver
    deploy(deployr, deployment, cloudrInstanceManager.getCurrentInstance()->getId());
  }

  // Reliqushing newly created instances from cloudr
  if (cloudrInstanceManager.getCurrentInstance()->isRootInstance())
    for (const auto &instance : newInstances) cloudrInstanceManager.terminateInstance(instance);

  // Finalizing cloudR
  cloudrInstanceManager.finalize();

  // Finalizing base instance manager
  instanceManager->finalize();
}