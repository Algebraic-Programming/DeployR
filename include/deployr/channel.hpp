#pragma once

#include <hicr/core/definitions.hpp>
#include <hicr/frontends/channel/variableSize/mpsc/locking/consumer.hpp>
#include <hicr/frontends/channel/variableSize/mpsc/locking/producer.hpp>

namespace deployr 
{

/**
 * Represents an input or output channel that allows DeployR instances to send variable-sized tokens to each other.
 * This channel may contain more than one producer, but always a single consumer.
 */
class Channel final 
{
    public:

    /// Represents a message exchanged between two instances through a channel
    struct token_t
    {
        /// Whether the exchange succeeded
        bool success;

        /// Pointer to the buffer, if the exchange succeeded 
        void* buffer;

        /// Size of the token received
        size_t size;
    };

    
    Channel() = delete;

    /**
     * Constructor for the Channel object. It requires passing all the elements it needs to execute at construction time.
     * 
     * @param[in] channelName The name of the channel
     * @param[in] memoryManager The memory manager to use to reserve buffer memory (consumer)
     * @param[in] memorySpace The memory space to use to reserve buffer memory (consumer)
     * @param[in] consumerInterface The interface for the consumer side of the channel. It is nullptr, if this instance is not a consumer of this channel
     * @param[in] producerInterface The interface for the producer side of the channel. It is nullptr, if this instance is not a producer of this channel
     * 
     * @note If this channel is accessible by an instance, it means that instance is either a consumer xor a producer.
     */
    Channel(
        const std::string channelName,
        HiCR::MemoryManager* const memoryManager,
        const std::shared_ptr<HiCR::MemorySpace> memorySpace,
        std::shared_ptr<HiCR::channel::variableSize::MPSC::locking::Consumer> consumerInterface,
        std::shared_ptr<HiCR::channel::variableSize::MPSC::locking::Producer> producerInterface
    ) :
     _memoryManager(memoryManager),
     _memorySpace(memorySpace),
     _consumerInterface(consumerInterface),
     _producerInterface(producerInterface)
    {}
    ~Channel() = default;

    __INLINE__ bool push(const void* buffer, const size_t size)
    {
        if (_producerInterface == nullptr) HICR_THROW_LOGIC("Attempting to push a message, but this instance has no producer role in channel '%s'\n", _channelName.c_str());

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
        if (_consumerInterface == nullptr) HICR_THROW_LOGIC("Attempting to peek a message, but this instance has no consumer role in channel '%s'\n", _channelName.c_str());

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
        if (_consumerInterface == nullptr) HICR_THROW_LOGIC("Attempting to pop a message, but this instance has no consumer role in channel '%s'\n", _channelName.c_str());

        // If the buffer is full, returning false
        if (_consumerInterface->isEmpty()) return false;

        // Popping buffer
        _consumerInterface->pop();

        // Returning true, as we succeeded
        return true;
    }

    private: 

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