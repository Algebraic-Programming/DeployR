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

  // Creating request with similar hardware setups
  deployr::Request request;
  request.addInstance(deployr::Request::Instance(0, "WorkerFc", topology));
  request.addInstance(deployr::Request::Instance(1, "WorkerFc", topology));
  request.addInstance(deployr::Request::Instance(2, "CoordinatorFc", topology));

  // Deploying request, getting deployment
  deployr.deploy(request, instances);
}