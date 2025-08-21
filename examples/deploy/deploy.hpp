#include <deployr/deployr.hpp>
#include <fstream>

void coordinatorFc(deployr::DeployR &deployr)
{
  // Getting local instance
  const auto &instance = deployr.getLocalInstance();
  printf("[CoordinatorFc] Hi, I am '%s'\n", instance.getName().c_str());
}

void workerFc(deployr::DeployR &deployr)
{
  // Getting local instance
  const auto &instance     = deployr.getLocalInstance();
  const auto &instanceName = instance.getName();
  printf("[WorkerFc] Hi, I am '%s'\n", instanceName.c_str());
}

void deploy(deployr::DeployR &deployr, const nlohmann::json& deployrConfigJs)
{
  // Registering Functions
  deployr.registerFunction("CoordinatorFc", [&]() { coordinatorFc(deployr); });
  deployr.registerFunction("WorkerFc", [&]() { workerFc(deployr); });

  // Initializing deployr
  deployr.initialize();

  // Creating request
  deployr::Request request(deployrConfigJs);

  // Deploying request, getting deployment
  deployr.deploy(request);
}