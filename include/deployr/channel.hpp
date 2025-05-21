#pragma once

#include <hicr/core/definitions.hpp>
#include <hicr/frontends/channel/variableSize/mpsc/locking/consumer.hpp>
#include <hicr/frontends/channel/variableSize/mpsc/locking/producer.hpp>

namespace deployr 
{

class Channel final 
{
    public:

    typedef size_t channelId_t;

    struct token_t
    {
        bool success;
        void* buffer;
        size_t size;
    };

    Channel() = delete;
    Channel(
        const channelId_t channelId,
        const std::string channelName,
        HiCR::MemoryManager* const memoryManager,
        const std::shared_ptr<HiCR::MemorySpace> memorySpace,
        std::shared_ptr<HiCR::channel::variableSize::MPSC::locking::Consumer> consumerInterface,
        std::shared_ptr<HiCR::channel::variableSize::MPSC::locking::Producer> producerInterface
    ) :
     _channelId(channelId),
     _memoryManager(memoryManager),
     _memorySpace(memorySpace),
     _consumerInterface(consumerInterface),
     _producerInterface(producerInterface)
    {}
    ~Channel() = default;

    __INLINE__ bool push(const void* buffer, const size_t size)
    {
        if (_producerInterface == nullptr) HICR_THROW_LOGIC("Attempting to push a message, but this instance has no producer role in channel '%s' (%lu)\n", _channelName.c_str(), _channelId);

        // If the buffer is full, returning false
        if (_producerInterface->isFull()) return false;

        // Using memory manager to register the buffer
        auto bufferMemorySlot = _memoryManager->registerLocalMemorySlot(_memorySpace, (void*)buffer, size);

        // Pushing buffer
        _producerInterface->push(bufferMemorySlot, 1);

        // Deregistering memory slot
        _memoryManager->deregisterLocalMemorySlot(bufferMemorySlot);

        // Returning true, as we succeeded
        return true;
    }

    [[nodiscard]] __INLINE__ const token_t peek()
    {
        if (_consumerInterface == nullptr) HICR_THROW_LOGIC("Attempting to peek a message, but this instance has no consumer role in channel '%s' (%lu)\n", _channelName.c_str(), _channelId);

        // If the buffer is full, returning false
        if (_consumerInterface->isEmpty()) return token_t { .success = false, .buffer = nullptr, .size = 0 };

        // Pushing buffer
        auto result = _consumerInterface->peek();

        // Getting absolute pointer to the token
        size_t tokenPos = result[0];
        size_t tokenSize = result[1];
        auto tokenBuffer = (uint8_t*)_consumerInterface->getPayloadBufferMemorySlot()->getSourceLocalMemorySlot()->getPointer();
        void* tokenPtr  = &tokenBuffer[tokenPos];

        // Returning a full message, as we succeeded
        return token_t { .success = true, .buffer = tokenPtr, .size = tokenSize };
    }

    __INLINE__ bool pop()
    {
        if (_consumerInterface == nullptr) HICR_THROW_LOGIC("Attempting to pop a message, but this instance has no consumer role in channel '%s' (%lu)\n", _channelName.c_str(), _channelId);

        // If the buffer is full, returning false
        if (_consumerInterface->isEmpty()) return false;

        // Poping buffer
        auto result = _consumerInterface->pop();

        // Returning true, as we succeeded
        return true;
    }

    private: 

    // Unique identifier of the channel
    const channelId_t _channelId;
    
    // Unique identifier of the channel
    const std::string _channelName;

    // Memory manager to use for memory slot registration
    HiCR::MemoryManager* const _memoryManager;

    // Memory space to use for memory slot registration
    const std::shared_ptr<HiCR::MemorySpace> _memorySpace;

    // Consumer interface
    const std::shared_ptr<HiCR::channel::variableSize::MPSC::locking::Consumer> _consumerInterface;
    const std::shared_ptr<HiCR::channel::variableSize::MPSC::locking::Producer> _producerInterface;
}; // class Channel

} // namespace deployr