#include <deployr/deployr.hpp>
#include <fstream>

void leaderFc(deployr::DeployR &deployr)
{
  // Getting local instance
  printf("[LeaderFc] Hi, I am instance id: %lu\n", deployr.getRunnerId());
}

void workerFc(deployr::DeployR &deployr)
{
  // Getting local instance
  printf("[WorkerFc] Hi, I am instance id: %lu\n", deployr.getRunnerId());
}

void deploy(deployr::DeployR &deployr, const deployr::Deployment &deployment, const HiCR::Instance::instanceId_t coordinatorInstanceId)
{
  // Initializing DeployR
  deployr.initialize();

  // Registering Functions
  deployr.registerFunction("LeaderFc", [&]() { leaderFc(deployr); });
  deployr.registerFunction("WorkerFc", [&]() { workerFc(deployr); });

  // Deploying now
  deployr.deploy(deployment, coordinatorInstanceId);
}