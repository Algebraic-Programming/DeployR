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

    Channel() = delete;
    Channel(
        const channelId_t channelId,
        std::shared_ptr<HiCR::channel::variableSize::MPSC::locking::Consumer> consumerInterface,
        std::shared_ptr<HiCR::channel::variableSize::MPSC::locking::Producer> producerInterface
    ) :
     _channelId(channelId),
     _consumerInterface(consumerInterface),
     _producerInterface(producerInterface)
    {}
    ~Channel() = default;

    private: 

    // Unique identifier of the channel
    channelId_t _channelId;

    // Consumer interface
    std::shared_ptr<HiCR::channel::variableSize::MPSC::locking::Consumer> _consumerInterface;
    std::shared_ptr<HiCR::channel::variableSize::MPSC::locking::Producer> _producerInterface;

}; // class Channel

} // namespace deployr