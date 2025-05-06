#pragma once

#include <hicr/core/communicationManager.hpp>
#include <hicr/core/instanceManager.hpp>
#include <hicr/core/memoryManager.hpp>
#include <memory>

namespace deployr 
{

class Engine
{
    public:

    Engine() = default;
    virtual ~Engine() = default;

    virtual void initialize(int* pargc, char*** pargv) = 0;
    virtual void abort() = 0;
    virtual void finalize() = 0;

    __INLINE__ bool isRootInstance() const { return _instanceManager->getCurrentInstance()->getId() == _instanceManager->getRootInstanceId(); }

    protected: 

    // Storage for the distributed engine's communication manager
    std::unique_ptr<HiCR::CommunicationManager> _communicationManager;

    // Storage for the distributed engine's instance manager
    std::unique_ptr<HiCR::InstanceManager> _instanceManager;

    // Storage for the distributed engine's memory manager
    std::unique_ptr<HiCR::MemoryManager> _memoryManager;

}; // class Engine

} // namespace deployr