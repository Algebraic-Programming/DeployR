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

void deploy(deployr::DeployR &deployr, const deployr::Deployment& deployment, const HiCR::Instance::instanceId_t coordinatorInstanceId)
{
  // Registering Functions
  deployr.registerFunction("CoordinatorFc", [&]() { coordinatorFc(deployr); });
  deployr.registerFunction("WorkerFc", [&]() { workerFc(deployr); });

  // Deploying now
  deployr.deploy(deployment, coordinatorInstanceId);
}