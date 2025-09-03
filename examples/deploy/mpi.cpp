#include <deployr/deployr.hpp>
#include <nlohmann_json/json.hpp>
#include <fstream>
#include <hicr/backends/mpi/instanceManager.hpp>
#include <hicr/backends/mpi/communicationManager.hpp>
#include <hicr/backends/mpi/memoryManager.hpp>
#include <hicr/frontends/RPCEngine/RPCEngine.hpp>
#include <hicr/backends/hwloc/topologyManager.hpp>
#include "deploy.hpp"

/**
 * Gets the global topology, the sum of all local topologies
 * 
 * @return A vector containing each of the local topologies, where the index corresponds to the host index in the getHiCRInstances function
 */
[[nodiscard]] __INLINE__ std::vector<nlohmann::json> gatherGlobalTopology(
  HiCR::frontend::RPCEngine* rpcEngine,
  HiCR::InstanceManager* instanceManager,
  const HiCR::Topology& localTopology)
{
  // Registering topology exchanging RPC
  auto gatherTopologyRPC = [&](void*) {
    // Serializing
    const auto serializedTopology = localTopology.serialize().dump();

    // Returning serialized topology
    rpcEngine->submitReturnValue((void *)serializedTopology.c_str(), serializedTopology.size() + 1);
  };

  // Registering RPC
  auto RPCExecutionUnit = HiCR::backend::pthreads::ComputeManager::createExecutionUnit(gatherTopologyRPC);

  // Adding RPC
  rpcEngine->addRPCTarget("Exchange Topology", RPCExecutionUnit);

  // Storage
  std::vector<nlohmann::json> globalTopology;
  const auto& currentInstance = instanceManager->getCurrentInstance();
  const bool isRootInstance = currentInstance->isRootInstance();
  const auto& instances = instanceManager->getInstances();

  // If I am not root and I am among the participating instances, then listen for the incoming RPC and return an empty topology
  if (isRootInstance == false)
  {
      rpcEngine->listen();
  }
  else // If I am root, request topology from all instances
  {
    for (const auto& instance : instances)
      if (instance->getId() == currentInstance->getId()) // If its me, just push my local topology
      {
        globalTopology.push_back(localTopology.serialize());
      }
      else // If not, it's another instance: send RPC and deserialize return value
      {
        // Requesting RPC from the remote instance
        rpcEngine->requestRPC(*instance, __DEPLOYR_GET_TOPOLOGY_RPC_NAME);

        // Getting return value as a memory slot
        auto returnValue = rpcEngine->getReturnValue(*instance);

        // Receiving raw serialized topology information from the worker
        std::string serializedTopology = (char *)returnValue->getPointer();

        // Parsing serialized raw topology into a json object
        auto topologyJson = nlohmann::json::parse(serializedTopology);

        // Freeing return value
        rpcEngine->getMemoryManager()->freeLocalMemorySlot(returnValue);

        // Pushing topology into the vector
        globalTopology.push_back(topologyJson);
      }
  }

  // Return global topology
  return globalTopology;
}

/**
 * Performs a matching between the requested runners and the available hosts
 * 
 * This implementation uses the Hopcroft-Karp algorithm to find a matching of all requested runners to a host.
 * 
 * @return True, if the matching was possible. False, otherwise.
 */
[[nodiscard]] __INLINE__ bool performMatching(const deployr::Deployment& request, const std::vector<HiCR::Topology*>& hosts)
{
  // /// The pairings map: runner name -> HiCR instance vector entry index
  // std::map<deployr::Runner::runnerId_t, HiCR::Topology*> pairings;

  // // Creating flat pairings vector
  // std::vector<deployr::Deployment::Pairing> pairingsVector;

  // // Creating one deployment runner per requested runner
  // for (const auto &requestedRunner : request.getRunners()) pairingsVector.push_back(deployr::Deployment::Pairing(requestedRunner.second.getId()));

  // // Building the matching graph
  // theAlgorithms::graph::HKGraph graph(pairingsVector.size(), hosts.size());
  // for (size_t i = 0; i < pairingsVector.size(); i++)
  //   for (size_t j = 0; j < hosts.size(); j++)
  //   {
  //     // Getting requested runner's name
  //     const auto &requestedRunnerName = pairingsVector[i].getRequestedRunnerId();

  //     // Getting requested runner's information
  //     const auto &requestedRunner = request.getRunners().at(requestedRunnerName);

  //     // Getting associated host type name
  //     const auto &requestedTopology = requestedRunner.getTopology();

  //     // Checking if the requested host type is compatible with the current host.
  //     // If so, add an edge to the graph
  //     if (hosts[j]->checkCompatibility(requestedTopology)) graph.addEdge(i, j);
  //   }

  // //  Finding out if a proper matching exists
  // auto matchCount = (size_t)graph.hopcroftKarpAlgorithm();

  // // If the number of matchings is smaller than requested, return false
  // if (matchCount < pairingsVector.size()) return false;

  // // Getting the pairings from the graph
  // const auto graphPairings = graph.getLeftSidePairings();
  // for (size_t i = 0; i < pairingsVector.size(); i++)
  // {
  //   auto        hostIdx           = (size_t)graphPairings[i + 1];
  //   const auto &requestedRunnerId = pairingsVector[i].getRequestedRunnerId();

  //   // Saving pairing in a map
  //   //printf("Pairing: %lu (%s) -> %lu\n", i, requestedRunnerName.c_str(), hostIdx);
  //   pairings[requestedRunnerId] = hosts[hostIdx];
  // }

  return true;
}

int main(int argc, char *argv[])
{
  // Getting MPI managers
  auto instanceManager      = HiCR::backend::mpi::InstanceManager::createDefault(&argc, &argv);
  auto communicationManager = std::make_shared<HiCR::backend::mpi::CommunicationManager>();
  auto memoryManager        = std::make_shared<HiCR::backend::mpi::MemoryManager>();

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

  // Calling main algorithm driver
  deploy(deployr, instances);

  // Finalizing instance manager
  instanceManager->finalize();
}