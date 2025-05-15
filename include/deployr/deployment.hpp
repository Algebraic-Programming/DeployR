#pragma once

#include <hicr/core/definitions.hpp>
#include <hopcroft_karp.hpp>
#include "request.hpp"

namespace deployr 
{

class Deployment final 
{
    public:

    typedef size_t resourceIdx_t;

    Deployment() = default;
    ~Deployment() = default;

    __INLINE__ void addMachine(const Request::Machine& machine) { _machines.push_back(machine); }
    __INLINE__ void addResource(const HiCR::Topology& topology) { _resources.push_back(topology); }

    __INLINE__ bool performMatching()
    {
        // Building the matching graph
        theAlgorithms::graph::HKGraph graph(_machines.size(), _resources.size());
        for (size_t i = 0; i < _machines.size(); i++)
            for (size_t j = 0; j < _resources.size(); j++)
                if (checkCompatibility(_machines[i], _resources[j])) graph.addEdge(i, j);

        //  Finding out if a proper matching exists
        auto matchCount = (size_t)graph.hopcroftKarpAlgorithm();
        //printf("Match Count: %d\n", matchCount);

        // If the number of matchings is smaller than requested, return false
        if (matchCount < _machines.size()) return false;

        // Getting the pairings from the graph
        _pairings.clear();
        const auto graphPairings = graph.getLeftSidePairings();
        _pairings.resize(_machines.size());
        for (size_t i = 1; i <= _machines.size(); i++)
        {
         auto machineIdx = i-1;
         auto resourceIdx = (size_t)graphPairings[i];
         //printf("Pairing: %lu -> %lu\n", machineIdx, resourceIdx);
         _pairings[machineIdx] = resourceIdx;
        }
         
        return true;
    }

    const std::vector<resourceIdx_t>& getPairings() const { return _pairings; }
    
    private: 

    __INLINE__ bool checkCompatibility(const Request::Machine& machine, const HiCR::Topology& resource)
    {
        ////////// Checking whether the resource contains the minimum host memory
        const auto minHostMemoryGB = machine.getMinHostMemoryGB();

        // Looking for NUMA Domain device to add up to the actual memory
        size_t actualHostMemoryBytes = 0;
        for (const auto& device : resource.getDevices())
            if (device->getType() == "NUMA Domain")
                for (const auto& memorySpace : device->getMemorySpaceList())
                    if (memorySpace->getType() == "RAM")
                        actualHostMemoryBytes = memorySpace->getSize();

        // Calculating GB
        const size_t actualHostMemoryGB = actualHostMemoryBytes / (1024ul * 1024ul * 1024ul);       
        
        // Returning false if not enough host memory found
        if (actualHostMemoryGB < minHostMemoryGB) return false;

        ////////// Checking whether the resource contains the minimum processing units
        const auto minHostProcessingUnits = machine.getMinHostProcessingUnits();

        // Looking for NUMA Domain device to add up the number of processing units
        size_t actualHostProcessingUnits = 0;
        for (const auto& device : resource.getDevices())
            if (device->getType() == "NUMA Domain")
                for (const auto& computeResource : device->getComputeResourceList())
                    if (computeResource->getType() == "Processing Unit")
                        actualHostProcessingUnits++;

        // Returning false if not enough processing units found
        if (actualHostProcessingUnits < minHostProcessingUnits) return false;
        // printf("Found %luGB - %lu PUs\n", actualHostMemoryGB, actualHostProcessingUnits);

        ////////// Checking for requested devices
        const auto requestedDevices = machine.getDevices();

        for (const auto& requestedDevice : requestedDevices)
        {
            const auto requestedDeviceType = requestedDevice.getType();
            const auto requestedDeviceCount = requestedDevice.getCount();

            // Looking for NUMA Domain device to add up the number of processing units
            size_t actualDeviceCount = 0;
            for (const auto& device : resource.getDevices())
            {
                // printf("Comparing %s to %s\n", device->getType().c_str(), requestedDeviceType.c_str());
                if (device->getType() == requestedDeviceType)
                    actualDeviceCount++;
            }
            // printf("Actual device Count: %lu\n", actualDeviceCount);
            // Failing if the require device count hasn't been met                     
            if (actualDeviceCount < requestedDeviceCount) return false;
        }

        // All requirements have been met, returning true
        //printf("Requirements met\n");
        return true;
    }

    // Requested machines
    std::vector<Request::Machine> _machines;

    // Provided resources
    std::vector<HiCR::Topology> _resources;

    // Pairings
    std::vector<resourceIdx_t> _pairings;
    
}; // class Deployment

} // namespace deployr