
#pragma once

#include <hicr/core/exceptions.hpp>
#include <hicr/core/definitions.hpp>
#include <nlohmann_json/json.hpp>
#include <nlohmann_json/parser.hpp>
#include <algorithm>
#include <vector>
#include "engine.hpp"
#include "request.hpp"
#include "deployment.hpp"

#ifdef _DEPLOYR_DISTRIBUTED_ENGINE_MPI
  #include "engines/mpi.hpp"
#endif

#ifdef _DEPLOYR_DISTRIBUTED_ENGINE_LOCAL
  #include "engines/local.hpp"
#endif

#define __DEPLOYR_GET_TOPOLOGY_RPC_NAME "[DeployR] Get Topology"
#define __DEPLOYR_GET_DEPLOYMENT_RPC_NAME "[DeployR] Get Deployment"

namespace deployr
{

class DeployR final
{
 public:

  DeployR()
  {
    // Instantiating distributed execution engine
    #ifdef _DEPLOYR_DISTRIBUTED_ENGINE_MPI
    _engine = std::make_unique<engine::MPI>();
    #endif

    #ifdef _DEPLOYR_DISTRIBUTED_ENGINE_LOCAL
    _engine = std::make_unique<engine::Local>();
    #endif
    
    // Registering topology exchanging RPC
    registerFunction(__DEPLOYR_GET_TOPOLOGY_RPC_NAME, [this]()
    {
        // Serializing
        const auto serializedTopology = _localTopology.dump();

        // Returning serialized topology
        _engine->submitRPCReturnValue((void*)serializedTopology.c_str(), serializedTopology.size());
    });

    // Registering deployment broadcasting RPC
    registerFunction(__DEPLOYR_GET_DEPLOYMENT_RPC_NAME, [this]()
    {
        // Serializing
        const auto serializedDeployment = _deployment.serialize().dump();

        // Returning serialized topology
        _engine->submitRPCReturnValue((void*)serializedDeployment.c_str(), serializedDeployment.size());
    });
  }

  ~DeployR() = default;

  __INLINE__ void initialize(int* pargc, char*** pargv)
  {
    // Initializing distributed execution engine
    _engine->initialize(pargc, pargv);

    // Getting local host index among all HiCR instances
    _localHostIndex = _engine->getLocalInstanceIndex();

    // Committing rpcs to the engine
    for (const auto& rpc : _registeredFunctions) _engine->registerRPC(rpc.first, rpc.second);

    // Getting local topology
    _localTopology = _engine->detectLocalTopology();

    // Gathering global topology into the root instance
    _globalTopology = gatherGlobalTopology();

    // If this is not the root instance, wait for incoming RPCs
    if (_engine->isRootInstance() == false) 
    {
      // Getting deployment information from the root instance
      _deployment = broadcastDeployment();
      //printf("Deployment Size: %lu (binary: %lu)\n", _deployment.serialize().dump().size(), nlohmann::json::to_cbor(_deployment.serialize()).size());

      // Identifying local instance
      identifyLocalInstance();

      // Creating communication channels
      createChannels();

      // Running initial function assigned to this host
      runInitialFunction();

      // Only a single user-defined RPC shall be executed by a non-root instance
      _engine->finalize();

      // Exiting regularly.
      exit(0);
    }
  }

  __INLINE__ void deploy(Request& request)
  {
    // Counting the exact number of instances requested.
    size_t instancesRequested = request.getInstances().size();

    // Getting the initial number of instances
    size_t HiCRInstanceCount = _engine->getHiCRInstances().size();

    // Printing instance count information
    // printf("[DeployR] Initial Instances:   %lu\n", HiCRInstanceCount);
    // printf("[DeployR] Requested Instances: %lu\n", instancesRequested);

    // We handle the following scenarios:
    // With N: number of requested instances
    // With K: number of initial instances
    
    // If K > N, more initial instances than requested provided. Abort execution.
    if (HiCRInstanceCount > instancesRequested)
    {
      fprintf(stderr, "[DeployR] More initial instances (%lu) provided than required (%lu) were provided.\n", HiCRInstanceCount, instancesRequested);
      _engine->abort();
    } 

    // If 1 < K < N, this is the hybrid scenario, not handled as it is complex and unlikely to be required. 
    if (HiCRInstanceCount > 1 && HiCRInstanceCount < instancesRequested)
    {
      fprintf(stderr, "[DeployR] Irregular number of initial instances (%lu) provided. Must be either 1 or %lu for this request.\n", HiCRInstanceCount, instancesRequested);
      _engine->abort();
    } 

    // If K == 1, this is the cloud scenario. N-1 instances will be created.
    if (HiCRInstanceCount == 1 && HiCRInstanceCount < instancesRequested)
    {
      fprintf(stderr, "[DeployR] TBD: create more instances according with requested instance hardware requirements\n");
      _engine->abort();
    } 

    // Printing topology
    // printf("[DeployR] Detected Global Topology\n");
    // for (size_t i = 0; i < _globalTopology.size(); i++)
    // {
    //   printf("Instance: %lu\n", i);
    //   printf("%s\n", _globalTopology[i].dump(2).c_str());
    //   printf("--------\n");
    // }

    // Building deployment object
    _deployment = Deployment(request);

    // Adding hosts to the deployment object
    for (size_t i = 0; i < _globalTopology.size(); i++) _deployment.addHost(Host(i, _globalTopology[i]));

    // Proceed with request to instance matching
    if (_deployment.performMatching() == false)
    {
      fprintf(stderr, "[DeployR] The provided resources are not sufficient for the requested instances.\n");
      _engine->abort();
    } 

    // Broadcasting deployment information to non-root instances
    broadcastDeployment();

    // Identifying this local instance
    identifyLocalInstance();

    // Creating communication channels
    createChannels();

    // Running initial function assigned to this host
    runInitialFunction();
  }

  __INLINE__ void registerFunction(const std::string& functionName, std::function<void()> fc)
  {
    // Checking if the RPC name was already used
    if (_registeredFunctions.contains(functionName) == true)
    {
        fprintf(stderr, "The function '%s' was already registered.\n", functionName.c_str());
        abort();
    } 

    // Adding new RPC to the set
    _registeredFunctions.insert({functionName, fc});
  }
  __INLINE__ HiCR::InstanceManager::instanceList_t getHiCRInstances() const { return _engine->getHiCRInstances(); }
  __INLINE__ void finalize() { _engine->finalize(); }
  __INLINE__ void abort() { _engine->abort(); }
  __INLINE__ bool isRootInstance() const { return _engine->isRootInstance(); }

  private:

  __INLINE__ void launchFunction(const size_t resourceIdx, const std::string& functionName)
  {
    if (_registeredFunctions.contains(functionName) == false)
    {
        fprintf(stderr, "The function RPC '%s' is not registered. Please register it before initializing DeployR.\n", functionName.c_str());
        abort();
    } 

    _engine->launchRPC(resourceIdx, functionName);
  }

  __INLINE__ std::vector<nlohmann::json> gatherGlobalTopology()
  {
      // Storage
      std::vector<nlohmann::json> globalTopology;

      // If I am not root, listen for the incoming RPC and return an empty topology
      if (isRootInstance() == false) _engine->listenRPCs(); 

      // If I am root, request topology from all instances
      else
       for (const auto& instance : _engine->getHiCRInstances())

          // If its the root instance (me), just push my local topology
          if (instance->isRootInstance() == true) globalTopology.push_back(_localTopology);

          // If not, it's another instance: send RPC and deserialize return value
          else
          {
              // Requessting RPC from the remote instance
              _engine->launchRPC(instance->getId(), __DEPLOYR_GET_TOPOLOGY_RPC_NAME);

              // Getting return value as a memory slot
              auto returnValue = _engine->getRPCReturnValue(*instance);

              // Receiving raw serialized topology information from the worker
              std::string serializedTopology = (char *)returnValue->getPointer();

              // Parsing serialized raw topology into a json object
              auto topologyJson = nlohmann::json::parse(serializedTopology);

              // Freeing return value
              _engine->freeRPCReturnValue(returnValue);

              // Pushing topology into the vector
              globalTopology.push_back(topologyJson);
          }
      
      // Return global topology
      return globalTopology;
  }

  __INLINE__ Deployment broadcastDeployment()
  {
      // Storage
      Deployment deployment;

      // If I am root
      if (isRootInstance() == true) 
      {
       // listen for the incoming RPCs and return an empty topology
       for (size_t i = 0; i < _engine->getHiCRInstances().size() - 1; i++) _engine->listenRPCs(); 

       // Copy my own deployment information
       deployment = _deployment;
      }

      // If I am not root, request deployment from root
      else
      {
          // Requessting RPC from the remote instance
          _engine->launchRPC(_engine->getRootInstanceIndex(), __DEPLOYR_GET_DEPLOYMENT_RPC_NAME);

          // Getting return value as a memory slot
          auto returnValue = _engine->getRPCReturnValue(_engine->getRootInstance());

          // Receiving raw serialized topology information from the worker
          std::string serializedDeployment = (char *)returnValue->getPointer();

          // Parsing serialized raw topology into a json object
          auto deploymentJson = nlohmann::json::parse(serializedDeployment);

          // Freeing return value
          _engine->freeRPCReturnValue(returnValue);

          // Pushing topology into the vector
          deployment = Deployment(deploymentJson);
      }

      // Return global topology
      return deployment;
  }

  __INLINE__ void createChannels()
  {
    // Getting original request
    const auto& request = _deployment.getRequest();

    // Getting set of channels from request
    const auto& channels = request.getChannels();

    // Creating each channel
    for (size_t i = 0; i < channels.size(); i++)
    {
      // Gettting reference to the current channel
      const auto& channel = channels[i];

      // Getting channel's name
      const auto& name = channel.getName();

      // Getting channel's producers
      const auto& producers = channel.getProducers();

      // Getting channel's consumer
      const auto& consumer = channel.getConsumer();

      // Getting buffer capacity (max token count)
      const auto& bufferCapacity = channel.getBufferCapacity();

      // Getting buffer size (bytes)
      const auto& bufferSize = channel.getBufferSize();

      // Getting producer HiCR instance list
      std::vector<HiCR::Instance::instanceId_t> producerInstances;
      for (const auto& producer : producers)
      {
        const auto instanceId = _deployment.getPairings().at(producer);
        producerInstances.push_back(instanceId);
      } 

      // Getting consumer instance id
      const auto& consumerInstance = _deployment.getPairings().at(consumer);
    
      // Getting channel unique id (required as a tag by HiCR channels)
      const auto channelId = i;
      // const auto& localInstanceName = _localInstance.getName();
      // printf("Instance '%s' - Creating channel %lu '%s'\n", localInstanceName.c_str(), channelId, channels[i].getName().c_str());
      
      // Creating channel object
      const auto channelObject = _engine->createChannel(channelId, name, producerInstances, consumerInstance, bufferCapacity, bufferSize);

      // Adding channel to map
      _channels[name] = channelObject;
    }
  }

  __INLINE__ void identifyLocalInstance()
  {
        // Getting pairings
        const auto& pairings = _deployment.getPairings();

        // Finding the pairing corresponding to this host
        std::string localInstanceName;
        for (const auto& p : pairings) if (p.second == _localHostIndex) { localInstanceName = p.first; break; }
    
        // Getting requested instance's information
        _localInstance = _deployment.getRequest().getInstances().at(localInstanceName);
  }

  __INLINE__ void runInitialFunction()
  {
    // Getting the function to run for the paired instance
    const auto fcName = _localInstance.getFunction();
    
    // Checking the requested function was registered
    if (_registeredFunctions.contains(fcName) == false)
    {
        fprintf(stderr, "The requested function name '%s' is not registered. Please register it before initializing DeployR.\n", fcName.c_str());
        abort();
    } 
    
    // Getting function pointer
    const auto& initialFc = _registeredFunctions[fcName];

    // Running initial function
    initialFc();
  }

  bool _continueExecuting = true;
  std::unique_ptr<Engine> _engine;

  std::map<std::string, std::function<void()>> _registeredFunctions;
  
  // Local instance object
  Request::Instance _localInstance;

  // Index of this host on the deployment
  size_t _localHostIndex;

  // Storage for the local system topology
  nlohmann::json _localTopology;

  // Storage for the global system topology
  std::vector<nlohmann::json> _globalTopology;

  // Map of channels
  std::map<std::string, std::shared_ptr<Channel>> _channels;

  Deployment _deployment;
}; // class DeployR

} // namespace deployr