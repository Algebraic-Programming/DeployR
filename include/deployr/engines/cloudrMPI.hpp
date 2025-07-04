#pragma once

#include <hicr/backends/cloudr/instanceManager.hpp>
#include <fstream>
#include "../engine.hpp"

namespace deployr::engine
{

/**
 * Represents an CloudR-based engine with the capability of both deploying multiple instances at launch time and runtime instance creation
 */
class CloudR final : public deployr::Engine
{
  public:

  CloudR()
    : deployr::Engine()
  {}

  ~CloudR() = default;

  __INLINE__ void initializeImpl(int *pargc, char ***pargv, std::function<void()> deploymentFc) override
  {
    _deploymentFc = deploymentFc;
    _cloudrInstanceManager = new HiCR::backend::cloudr::InstanceManager([this](HiCR::backend::cloudr::InstanceManager* cloudr, int, char**) { _deploymentFc(); return 0; });
    
    _instanceManager      = _cloudrInstanceManager;
    _cloudrInstanceManager->initialize(pargc, pargv, [this]()
    {
     _communicationManager = _cloudrInstanceManager->getCommunicationManager().get();
     _memoryManager        = _cloudrInstanceManager->getMemoryManager().get(); 
    });

    // The following will only be executed by the root instance

    // Getting request file name from arguments
    const char* cloudrConfigFilePath = std::getenv("DEPLOYR_CLOUDR_CONFIG_FILE_PATH");
    if (cloudrConfigFilePath == nullptr) 
    {
      fprintf(stderr, "Required environment variable 'DEPLOYR_CLOUDR_CONFIG_FILE_PATH' not provided\n");
      abort();
    }

    // Parsing request file contents to a JSON object
    std::ifstream ifs(cloudrConfigFilePath);
    if (ifs.is_open() == false)
    {
      fprintf(stderr, "Config file '%s' provided in environment variable 'DEPLOYR_CLOUDR_CONFIG_FILE_PATH' could not be opened.\n", cloudrConfigFilePath);
      abort();
    }
    auto  cloudrConfigJs = nlohmann::json::parse(ifs);

    // Configuring emulated instance topologies
    _cloudrInstanceManager->setInstanceTopologies(cloudrConfigJs);
  };

  __INLINE__ void finalize() override {
     _instanceManager->finalize();
     delete _instanceManager;
   }

  __INLINE__ void abort() override { _instanceManager->abort(-1); }

    __INLINE__ void deploy() override
  {
    // Instantiating RPC engine
    _rpcEngine = _cloudrInstanceManager->getRPCEngine();
  }

  private:
  
  HiCR::backend::cloudr::InstanceManager* _cloudrInstanceManager;
  std::function<void()> _deploymentFc;
};

} // namespace deployr::engine