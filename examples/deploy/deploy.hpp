#include <deployr/deployr.hpp>
#include <fstream>

void coordinatorFc(deployr::DeployR &deployr)
{
  // Getting local instance
  printf("[CoordinatorFc] Hi, I am instance id: %lu\n", deployr.getInstanceId());
}

void workerFc(deployr::DeployR &deployr)
{
  // Getting local instance
  printf("[WorkerFc] Hi, I am instance id: %lu\n", deployr.getInstanceId());
}

void deploy(deployr::DeployR &deployr, const std::vector<HiCR::Instance*>& instances)
{
  // Registering Functions
  deployr.registerFunction("CoordinatorFc", [&]() { coordinatorFc(deployr); });
  deployr.registerFunction("WorkerFc", [&]() { workerFc(deployr); });

  // Initializing deployr
  deployr.initialize();

  // Getting local topology
  hwloc_topology_t hwlocTopology;
  hwloc_topology_init(&hwlocTopology);
  auto hwlocTopologyManager = HiCR::backend::hwloc::TopologyManager(&hwlocTopology);
  const auto& topology           = hwlocTopologyManager.queryTopology();

  // Sanity check for enough instances
  if (instances.size() != 3)
  {
    fprintf(stderr, "Error: this example requires three instances to run.\n");
    exit(-1);
  }

  // Creating request with similar hardware setups
  deployr::Deployment deployment;
  deployment.addRunner(deployr::Runner(0, "WorkerFc", topology, instances[0]->getId()));
  deployment.addRunner(deployr::Runner(1, "WorkerFc", topology, instances[1]->getId()));
  deployment.addRunner(deployr::Runner(2, "CoordinatorFc", topology, instances[2]->getId()));

  // Deploying request, getting deployment
  deployr.deploy(deployment);
}