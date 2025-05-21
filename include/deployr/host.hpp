#pragma once

#include <hicr/core/definitions.hpp>
#include "request.hpp"

namespace deployr 
{

class Host final 
{
    public:

    Host() = default;
    Host(const size_t hostIndex, const nlohmann::json& topology) : _hostIndex(hostIndex), _topology(topology) {}
    ~Host() = default;

    [[nodiscard]] __INLINE__ bool checkCompatibility(const Request::HostType& request)
    {
        ////////// Checking whether the _topology contains the minimum host memory
        const auto minHostMemoryGB = request.getMinMemoryGB();

        // Getting topology's devices
        const auto& devices = hicr::json::getArray<nlohmann::json>(_topology, "Devices");

        // Looking for NUMA Domain device to add up to the actual memory
        size_t actualHostMemoryBytes = 0;
        for (const auto& device : devices)
        {
            const auto& deviceType = hicr::json::getString(device, "Type");
            if (deviceType == "NUMA Domain")
            {
                const auto& memorySpaces = hicr::json::getArray<nlohmann::json>(device, "Memory Spaces");
                for (const auto& memorySpace : memorySpaces)
                {
                    const auto& memorySpaceType = hicr::json::getString(memorySpace, "Type");
                    if (memorySpaceType == "RAM")
                    {
                        const auto& memorySpaceSize = hicr::json::getNumber<size_t>(memorySpace, "Size");
                        actualHostMemoryBytes = memorySpaceSize;
                    }
                }
            }
        }

        // Calculating GB
        const size_t actualHostMemoryGB = actualHostMemoryBytes / (1024ul * 1024ul * 1024ul);       
        
        // Returning false if not enough host memory found
        if (actualHostMemoryGB < minHostMemoryGB) return false;

        ////////// Checking whether the _topology contains the minimum processing units
        const auto minHostProcessingUnits = request.getMinProcessingUnits();

        // Looking for NUMA Domain device to add up the number of processing units
        size_t actualHostProcessingUnits = 0;
        for (const auto& device : devices)
        {
            const auto& deviceType = hicr::json::getString(device, "Type");
            if (deviceType == "NUMA Domain")
            {
                const auto& computeResources = hicr::json::getArray<nlohmann::json>(device, "Compute Resources");
                for (const auto& computeResource : computeResources)
                {
                    const auto& computeResourceType = hicr::json::getString(computeResource, "Type");
                    if (computeResourceType == "Processing Unit") actualHostProcessingUnits++;
                }
            }
        }

        // Returning false if not enough processing units found
        if (actualHostProcessingUnits < minHostProcessingUnits) return false;
        // printf("Found %luGB - %lu PUs\n", actualHostMemoryGB, actualHostProcessingUnits);

        ////////// Checking for requested devices
        const auto requestedDevices = request.getDevices();

        for (const auto& requestedDevice : requestedDevices)
        {
            const auto requestedDeviceType = requestedDevice.getType();
            const auto requestedDeviceCount = requestedDevice.getCount();

            // Looking for NUMA Domain device to add up the number of processing units
            size_t actualDeviceCount = 0;
            for (const auto& device : devices)
            {
                const auto& deviceType = hicr::json::getString(device, "Type");

                // printf("Comparing %s to %s\n", device->getType().c_str(), requestedDeviceType.c_str());
                if (deviceType == requestedDeviceType) actualDeviceCount++;
            }

            // printf("Actual device Count: %lu\n", actualDeviceCount);
            // Failing if the require device count hasn't been met                     
            if (actualDeviceCount < requestedDeviceCount) return false;
        }

        // All requirements have been met, returning true
        //printf("Requirements met\n");
        return true;
    }

    [[nodiscard]] const size_t getHostIndex() const { return _hostIndex; }
    [[nodiscard]] const nlohmann::json& getTopology() const { return _topology; }

    [[nodiscard]] __INLINE__ nlohmann::json serialize() const
    {
        // Creating host JSON object
        nlohmann::json hostJs;

        // Getting deployment time
        hostJs["Host Index"] = _hostIndex;

        // Serializing request
        hostJs["Topology"] = _topology;

        return hostJs;
    }

    Host(const nlohmann::json& hostJs)
    {
        // Deserializing information
        _hostIndex = hicr::json::getNumber<size_t>(hostJs, "Host Index");
        _topology = hicr::json::getObject(hostJs, "Topology");
    }

    private: 

    // Index of the corresponding host within the instance manager's instance vector
    size_t _hostIndex;

    // Host's actual topology, in JSON format
    nlohmann::json _topology;

}; // class Host

} // namespace deployr