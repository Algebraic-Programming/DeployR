#pragma once

#include <hicr/core/definitions.hpp>

namespace deployr 
{

class Channel final 
{
    public:

    typedef size_t channelId_t;

    Channel() = default;
    Channel(const channelId_t channelId) : _channelId(channelId) {}
    ~Channel() = default;

    private: 

    // Unique identifier of the channel
    channelId_t _channelId;

}; // class Channel

} // namespace deployr