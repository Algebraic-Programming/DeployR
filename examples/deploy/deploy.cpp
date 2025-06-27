#include <deployr/deployr.hpp>
#include <nlohmann_json/json.hpp>
#include <fstream>
#include "coordinator.hpp"
#include "worker.hpp"

int main(int argc, char *argv[])
{
  // Initialize Hwloc topology object
  hwloc_topology_t hwlocTopology;
  hwloc_topology_init(&hwlocTopology);

  // Initializing HWLoc-based host (CPU) topology manager
  auto topologyManager = HiCR::backend::hwloc::TopologyManager(&hwlocTopology);

  // Creating DeployR instance
  deployr::DeployR deployr;

  // Add topology manager
  deployr.addTopologyManager(&topologyManager);

  // Creating Functions
  deployr.registerFunction("CoordinatorFc", [&]() { coordinatorFc(deployr); });
  deployr.registerFunction("WorkerFc", [&]() { workerFc(deployr); });

  // Initializing DeployR.
  bool isRoot = deployr.initialize(&argc, &argv);

  // Only one instance (root) configures and runs the deployment
  if (isRoot)
  {
    // Checking arguments
    if (argc != 2)
    {
      fprintf(stderr, "Error: Must provide the request file as argument.\n");
      deployr.abort();
      return -1;
    }

    // Getting request file name from arguments
    std::string requestFilePath = std::string(argv[1]);

    // Parsing request file contents to a JSON object
    std::ifstream ifs(requestFilePath);
    auto          requestJs = nlohmann::json::parse(ifs);

    // Creating request
    deployr::Request request(requestJs);

    // Deploying request, getting deployment
    deployr.deploy(request);
  }

  // Finalize
  deployr.finalize();
}