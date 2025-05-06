#pragma once

#include <vector>
#include <string>

namespace deployr 
{

class Request final 
{
    public:

    struct device_t 
    {
      std::string type;
      size_t _count;
    };

    Request
    (
      const size_t minCPUMemoryGB,
      const size_t minCPUProcessingUnits,
      const std::vector<device_t>& devices
    ) :
    _minCPUMemoryGB(minCPUMemoryGB),
    _minCPUProcessingUnits(minCPUProcessingUnits),
    _devices(devices)
    {

    }

    virtual ~Request() = default;

    private: 

    const size_t _minCPUMemoryGB;
    const size_t _minCPUProcessingUnits;
    const std::vector<device_t> _devices;

}; // class Request

} // namespace deployr