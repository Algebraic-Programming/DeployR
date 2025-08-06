#include <deployr/deployr.hpp>
#include <nlohmann_json/json.hpp>
#include <fstream>
#include <hicr/backends/cloudr/instanceManager.hpp>
#include <deployr/deployr.hpp>
#include "driver.hpp"

int cloudRMain(HiCR::backend::cloudr::InstanceManager *cloudr, int argc, char *argv[])
{
  // Getting CloudR managers
  auto communicationManager = cloudr->getCommunicationManager().get();
  auto memoryManager        = cloudr->getMemoryManager().get(); 

  // Getting CloudR's RPC engine
  auto rpcEngine = cloudr->getRPCEngine();

  // Creating deployr object
  deployr::DeployR deployr(cloudr, communicationManager, memoryManager, rpcEngine);

  // File path to deployr's config
  auto deployrConfigFilePath = argv[1];

  // Calling main algorithm driver
  driver(deployr, deployrConfigFilePath);

  return 0;
}

int main(int argc, char *argv[])
{
  HiCR::backend::cloudr::InstanceManager cloudr(cloudRMain);
  cloudr.initialize(&argc, &argv);

  // Checking if I'm root
  bool isRoot = cloudr.getCurrentInstance()->getId() == cloudr.getRootInstanceId();

  // Only do the following if I'm root
  if (isRoot)
  {
    // Checking arguments
    if (argc != 3)
    {
      fprintf(stderr, "Error: Must provide (1) a DeployR JSON configuration file, (2) a CloudR JSON configuration file.\n");
      cloudr.abort(-1);
      return -1;
    }

    // Getting CloudR configuration file name from arguments
    std::string cloudrConfigurationFilePath = std::string(argv[2]);

    // Parsing CloudR configuration file contents to a JSON object
    std::ifstream ifs(cloudrConfigurationFilePath);
    auto          cloudrConfigurationJs = nlohmann::json::parse(ifs);

    // Configuring emulated instance topologies
    cloudr.setConfiguration(cloudrConfigurationJs);

    // Calling what's supposed to be cloudR main
    cloudRMain(&cloudr, argc, argv);
  }

  // Finalizing cloudr
  cloudr.finalize();
}

// int main(int argc, char *argv[])
// {
  // // Creating DeployR instance
  // deployr::DeployR deployr;

  // // Creating Functions
  // deployr.registerFunction("CoordinatorFc", [&]() { coordinatorFc(deployr); });
  // deployr.registerFunction("WorkerFc", [&]() { workerFc(deployr); });

  // // Initializing DeployR. 
  // bool isRoot = deployr.initialize(&argc, &argv);

  // // Only one instance (root) configures and runs the deployment
  // if (isRoot)
  // {
  //   // Checking arguments
  //   if (argc != 2)
  //   {
  //     fprintf(stderr, "Error: Must provide the request file as argument.\n");
  //     deployr.abort();
  //     return -1;
  //   }

  //   // Getting request file name from arguments
  //   std::string requestFilePath = std::string(argv[1]);

  //   // Parsing request file contents to a JSON object
  //   std::ifstream ifs(requestFilePath);
  //   auto          requestJs = nlohmann::json::parse(ifs);

  //   // Creating request
  //   deployr::Request request(requestJs);

  //   // Deploying request, getting deployment
  //   deployr.deploy(request);
  // }

  // // Finalize
  // deployr.finalize();
// }