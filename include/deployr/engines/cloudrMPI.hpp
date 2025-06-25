#pragma once

#include <hicr/backends/cloudr/instanceManager.hpp>
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

  __INLINE__ void initialize(int *pargc, char ***pargv, std::function<void()> deploymentFc) override
  {
    _deploymentFc = deploymentFc;
    _cloudrInstanceManager = new HiCR::backend::cloudr::InstanceManager([this](HiCR::backend::cloudr::InstanceManager* cloudr, int, char**) { _deploymentFc(); return 0; });
    _cloudrInstanceManager->initialize(pargc, pargv);
    _instanceManager      = _cloudrInstanceManager;
    _communicationManager = _cloudrInstanceManager->getCommunicationManager().get();
    _memoryManager        = _cloudrInstanceManager->getMemoryManager().get();
  };

  __INLINE__ void finalize() override {
     _instanceManager->finalize();
     delete _instanceManager;
   }

  __INLINE__ void abort() override { _instanceManager->abort(-1); }

  private:
  
  HiCR::backend::cloudr::InstanceManager* _cloudrInstanceManager;
  std::function<void()> _deploymentFc;
};

} // namespace deployr::engine