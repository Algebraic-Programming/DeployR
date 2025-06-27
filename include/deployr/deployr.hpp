
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

#ifdef _DEPLOYR_DISTRIBUTED_ENGINE_CLOUDR_MPI
  #include "engines/cloudrMPI.hpp"
#endif

#define __DEPLOYR_GET_TOPOLOGY_RPC_NAME "[DeployR] Get Topology"
#define __DEPLOYR_GET_DEPLOYMENT_RPC_NAME "[DeployR] Get Deployment"

namespace deployr
{

/**
 * Main runtime class of DeployR. This is the only entry point for end-users.Channel
 * It provides the functionality to deploy a deployment request and give access to the channels created among the created instances.
 */
class DeployR final
{
  public:

  /**
   * Default constructor for DeployR. It creates the HiCR management engine and registers the basic functions needed during deployment.
   */
  DeployR()
  {
// Instantiating distributed execution engine
#ifdef _DEPLOYR_DISTRIBUTED_ENGINE_MPI
    _engine = std::make_unique<engine::MPI>();
#endif

#ifdef _DEPLOYR_DISTRIBUTED_ENGINE_LOCAL
    _engine = std::make_unique<engine::Local>();
#endif

#ifdef _DEPLOYR_DISTRIBUTED_ENGINE_CLOUDR_MPI
    _engine = std::make_unique<engine::CloudR>();
#endif

    // Registering topology exchanging RPC
    registerFunction(__DEPLOYR_GET_TOPOLOGY_RPC_NAME, [this]() {
      // Serializing
      const auto serializedTopology = _localTopology.dump();

      // Returning serialized topology
      _engine->submitRPCReturnValue((void *)serializedTopology.c_str(), serializedTopology.size() + 1);
    });

    // Registering deployment broadcasting RPC
    registerFunction(__DEPLOYR_GET_DEPLOYMENT_RPC_NAME, [this]() {
      // Serializing
      const auto serializedDeployment = _deployment.serialize().dump();

      // Returning serialized topology
      _engine->submitRPCReturnValue((void *)serializedDeployment.c_str(), serializedDeployment.size() + 1);
    });
  }

  ~DeployR() = default;

  /**
   * Add topology manager to the engine
   * 
   * @param topologyManager topology manager to add to Engine
  */
  __INLINE__ void addTopologyManager(HiCR::TopologyManager *topologyManager) { _engine->addTopologyManager(topologyManager); }

  /**
   * The initialization function for DeployR.
   * 
   * It initializes the HiCR management engine, detects the local topology, broadcasts the global topology to the root instance.
   * All instances need to be call this function before the root instance can request a deployment. Only the root instance will continue thereafter. 
   * 
   * @param[in] pargc A pointer to the argc value given in main. Its value is initialized at this point. Using it before will result in undefined behavior.
   * @param[in] pargv A pointer to the argv value given in main. Its value is initialized at this point. Using it before will result in undefined behavior.
   * @return True, if this is the root rank; False, otherwise
   */
  __INLINE__ bool initialize(int *pargc, char ***pargv)
  {
    // Function for deployment after initializing a new instance
    auto deploymentFc = [this]() {
      // Running deployment function of the engine
      _engine->deploy();

      // Getting local host index among all HiCR instances
      _localHostIndex = _engine->getLocalInstanceIndex();

      // Committing rpcs to the engine
      for (const auto &rpc : _registeredFunctions) _engine->registerRPC(rpc.first, rpc.second);

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
        _localInstance = identifyLocalInstance();

        // Creating communication channels
        createChannels();

        // Running initial function assigned to this host
        runInitialFunction();
      }
    };

    // Initializing distributed execution engine
    _engine->initialize(pargc, pargv, deploymentFc);

    // Return whether this is the root instance or not
    return _engine->isRootInstance();
  }

  /**
   * Attempts to deploy a deployment request. 
   * 
   * If not enough (or too many) hosts are detected than the request needs, it will abort execution.
   * If a good mapping is found, it will run each of the requested instance in one of the found hosts and run its initial function.
   * 
   * @param[in] request A request object containing all the required instances and channels that make a deployment.
   */
  __INLINE__ void deploy(Request &request)
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
      fprintf(stderr,
              "[DeployR] The detected hosts (%lu) are either not sufficient for the requested instances (%lu) or their topology doesn't satisfy that of the requested instances.\n",
              HiCRInstanceCount,
              instancesRequested);
      _engine->abort();
    }

    // Broadcasting deployment information to non-root instances
    broadcastDeployment();

    // Identifying this local instance
    _localInstance = identifyLocalInstance();

    // Creating communication channels
    createChannels();

    // Running initial function assigned to this host
    runInitialFunction();
  }

  /**
   * Registers a function that can be a target as initial function for one or more requested instances.
   * 
   * If a requested initial function is not registered by this function, deployment will fail.
   * 
   * @param[in] functionName The name of the function to register. This value will be used to match against the requested instance functions
   * @param[in] fc The actual function to register
   * 
   */
  __INLINE__ void registerFunction(const std::string &functionName, std::function<void()> fc)
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

  /**
   * Retrieves one of the channels creates during deployment.
   * 
   * If the provided name is not registered for this instance, the function will produce an exception.
   * 
   * @param[in] name The name of the channel to retrieve
   * @return The requested channel
   */
  __INLINE__ Channel &getChannel(const std::string &name)
  {
    if (_channels.contains(name) == false) HICR_THROW_LOGIC("Requested channel ('%s') is not defined for this instance ('%s')\n", name.c_str(), _localInstance.getName().c_str());

    return _channels.at(name).operator*();
  }

  /**
   * Retrieves a deployment object with the information about the deployment (pairings, hosts, channels, request)
   * 
   * This function must be called only after deploying
   * 
   * @return The deployment object
   */
  [[nodiscard]] __INLINE__ const Deployment &getDeployment() const { return _deployment; }

  /**
   * Retrieves the instance request corresponding to the local running instance
   * 
   * This function must be called only after initializing
   * 
   * @return The instance request object
   */
  [[nodiscard]] __INLINE__ const Request::Instance &getLocalInstance() const { return _localInstance; }

  /**
   * Finalizes the deployment. Must be called by the root instance before exiting the applicataion
   */
  __INLINE__ void finalize() { _engine->finalize(); }

  /**
   * Fatally aborts execution. Must be used only in case of unsalvageable errors.
   */
  __INLINE__ void abort() { _engine->abort(); }

  /**
   * Retrieves the RPC engine for direct use
   * 
   * @return A pointer to the internal RPC engine
   */
  __INLINE__ HiCR::frontend::RPCEngine *getRPCEngine() { return _engine->getRPCEngine(); }

  private:

  /**
   * [Internal] Gets an array of HiCR instances, each one representing a Host
   * 
   * @return An array containing the HiCR instances detected/created
   */
  __INLINE__ HiCR::InstanceManager::instanceList_t getHiCRInstances() const { return _engine->getHiCRInstances(); }

  /**
   * [Internal] Checks whether the currently running instance is root
   * 
   * @return true, if this is a root instance. false, otherwise.
   */
  [[nodiscard]] __INLINE__ bool isRootInstance() const { return _engine->isRootInstance(); }

  /**
   * [Internal] Launches a provided function on the requested host
   * 
   * @param[in] hostIdx The index within the HiCR instanceList_t corresponding to the host that should execute the function (RPC)
   * @param[in] functionName The name of the function to run. It must be registered on the target host before running
   */
  __INLINE__ void launchFunction(const size_t hostIdx, const std::string &functionName)
  {
    if (_registeredFunctions.contains(functionName) == false)
    {
      fprintf(stderr, "The function RPC '%s' is not registered. Please register it before initializing DeployR.\n", functionName.c_str());
      abort();
    }

    _engine->launchRPC(hostIdx, functionName);
  }

  /**
   * [Internal] Gets the global topology, the sum of all local topologies
   * 
   * @return A vector containing each of the local topologies, where the index corresponds to the host index in the getHiCRInstances function
   */
  [[nodiscard]] __INLINE__ std::vector<nlohmann::json> gatherGlobalTopology()
  {
    // Storage
    std::vector<nlohmann::json> globalTopology;

    // If I am not root, listen for the incoming RPC and return an empty topology
    if (isRootInstance() == false) _engine->listenRPCs();

    // If I am root, request topology from all instances
    else
      for (const auto &instance : _engine->getHiCRInstances())

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

  /**
   * [Internal] Makes the root instance broadcast the serialized deployment information to all others and deserializes it locally.
   * 
   * @return The deserialized Deployment object
   */
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

  /**
   * Creates all requested channels in the request
   */
  __INLINE__ void createChannels()
  {
    // Getting original request
    const auto &request = _deployment.getRequest();

    // Getting set of channels from request
    const auto &channels = request.getChannels();

    // Creating each channel
    for (size_t i = 0; i < channels.size(); i++)
    {
      // Gettting reference to the current channel
      const auto &channel = channels[i];

      // Getting channel's name
      const auto &name = channel.getName();

      // Getting channel's producers
      const auto &producers = channel.getProducers();

      // Getting channel's consumer
      const auto &consumer = channel.getConsumer();

      // Getting buffer capacity (max token count)
      const auto &bufferCapacity = channel.getBufferCapacity();

      // Getting buffer size (bytes)
      const auto &bufferSize = channel.getBufferSize();

      // Getting producer HiCR instance list
      std::vector<HiCR::Instance::instanceId_t> producerInstances;
      for (const auto &producer : producers)
      {
        const auto instanceId = _deployment.getPairings().at(producer);
        producerInstances.push_back(instanceId);
      }

      // Getting consumer instance id
      const auto &consumerInstance = _deployment.getPairings().at(consumer);

      // Getting channel unique id (required as a tag by HiCR channels)
      const auto channelId = i;
      // const auto& localInstanceName = _localInstance.getName();
      // printf("Instance '%s' - Creating channel %lu '%s'\n", localInstanceName.c_str(), channelId, channels[i].getName().c_str());

      // Creating channel object
      const auto channelObject = _engine->createChannel(channelId, name, producerInstances, consumerInstance, bufferCapacity, bufferSize);

      // Adding channel to map, only if defined
      if (channelObject != nullptr) _channels[name] = channelObject;
    }
  }

  /**
   * [Internal] Detects which of the instances in the request corresponds to this local instance
   * 
   * @return An instance request object corresponding to the local instance
   */
  __INLINE__ deployr::Request::Instance identifyLocalInstance()
  {
    // Getting pairings
    const auto &pairings = _deployment.getPairings();

    // Finding the pairing corresponding to this host
    std::string localInstanceName;
    for (const auto &p : pairings)
      if (p.second == _localHostIndex)
      {
        localInstanceName = p.first;
        break;
      }

    // Getting requested instance's information
    return _deployment.getRequest().getInstances().at(localInstanceName);
  }

  /**
 * [Internal] Runs the initial function assigned to this instance
 */
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
    const auto &initialFc = _registeredFunctions[fcName];

    // Running initial function
    initialFc();
  }

  /// A pointer to the HiCR Manager Engine used to run low-level operations
  std::unique_ptr<Engine> _engine;

  /// A map of registered functions, targets for an instance's initial function
  std::map<std::string, std::function<void()>> _registeredFunctions;

  /// Local instance object
  Request::Instance _localInstance;

  /// Index of this host on the deployment
  size_t _localHostIndex;

  /// Storage for the local system topology
  nlohmann::json _localTopology;

  /// Storage for the global system topology
  std::vector<nlohmann::json> _globalTopology;

  /// Map of created channels
  std::map<std::string, std::shared_ptr<Channel>> _channels;

  // Object containing the information of the deployment
  Deployment _deployment;

}; // class DeployR

} // namespace deployr