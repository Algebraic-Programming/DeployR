#pragma once

#include <hicr/core/definitions.hpp>
#include <hopcroft_karp.hpp>
#include "request.hpp"
#include "host.hpp"

namespace deployr 
{

class Deployment final 
{
    public:

    // Represents a deployed instance
    class Instance final
    {
        public: 

        Instance(Request::Instance requestedInstance) : _requestedInstance(requestedInstance) {}

        const Request::Instance& getRequestedInstance() const { return _requestedInstance; }
        const Host& getAssignedHost() const { return _assignedHost; }
        void setAssignedHost(const Host& assignedHost) { _assignedHost = assignedHost; }

        private:

        const Request::Instance _requestedInstance;
        Host _assignedHost;
    };

    Deployment() = delete;
    Deployment(Request request) : _request(request)
    {
        for (const auto& requestedInstance : _request.getInstances())
            _instances.push_back(Instance(requestedInstance.second));
    };
    ~Deployment() = default;

    __INLINE__ void addHost(const Host& host) { _hosts.push_back(host); }

    __INLINE__ bool performMatching()
    {
        // Building the matching graph
        theAlgorithms::graph::HKGraph graph(_instances.size(), _hosts.size());
        for (size_t i = 0; i < _instances.size(); i++)
            for (size_t j = 0; j < _hosts.size(); j++)
            {
                // Getting requested instance
                const auto& requestedInstance = _instances[i].getRequestedInstance();

                // Getting associated host type name
                const auto& requestedHostTypeName = requestedInstance.getHostType();

                // Getting actual host type object
                const auto& requestedHostType = _request.getHostTypes().at(requestedHostTypeName);

                // Checking if the requested host type is compatible with the current host.
                // If so, add an edge to the graph
                if (_hosts[j].checkCompatibility(requestedHostType)) graph.addEdge(i, j);
            }

        //  Finding out if a proper matching exists
        auto matchCount = (size_t)graph.hopcroftKarpAlgorithm();
        //printf("Match Count: %d\n", matchCount);

        // If the number of matchings is smaller than requested, return false
        if (matchCount < _instances.size()) return false;

        // Getting the pairings from the graph
        const auto graphPairings = graph.getLeftSidePairings();
        for (size_t i = 0; i < _instances.size(); i++)
        {
         auto hostIdx = (size_t)graphPairings[i+1];
         printf("Pairing: %lu -> %lu\n", i, hostIdx);
         _instances[i].setAssignedHost(_hosts[hostIdx]);
        }
         
        return true;
    }

    __INLINE__ const std::vector<Host>& getHosts() const { return _hosts; }
    __INLINE__ const std::vector<Instance>& getInstances() const { return _instances; }
    __INLINE__ const Request& getRequest() const { return _request; }

    private: 

    // Request to fulfill
    const Request _request;

    // The deployed instance vector
    std::vector<Instance> _instances;

    // Provided hosts
    std::vector<Host> _hosts;
    
}; // class Deployment

} // namespace deployr