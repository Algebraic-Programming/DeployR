#pragma once

#include <hicr/core/definitions.hpp>
#include <hopcroft_karp.hpp>
#include "request.hpp"
#include "host.hpp"
#include "common.hpp"

namespace deployr 
{

/**
 * Represents the result of launching a deployment request. It contains the pairings between requested instances and the host that will contain them.
 */
class Deployment final 
{
    public:

    Deployment() = default;
    Deployment(Request request) : _request(request)
    {
        // Setting start time at the creation of this object
        _deployStartTime = getCurrentDateTime();
    };
    ~Deployment() = default;

    __INLINE__ void addHost(const Host& host) { _hosts.push_back(host); }

    [[nodiscard]] __INLINE__ bool performMatching()
    {
        // Creating flat pairings vector
        std::vector<Pairing> pairingsVector;

        // Creating one deployment instance per requested instance
        for (const auto& requestedInstance : _request.getInstances())
            pairingsVector.push_back(Pairing(requestedInstance.second.getName()));

        // Building the matching graph
        theAlgorithms::graph::HKGraph graph(pairingsVector.size(), _hosts.size());
        for (size_t i = 0; i < pairingsVector.size(); i++)
            for (size_t j = 0; j < _hosts.size(); j++)
            {
                // Getting requested instance's name
                const auto& requestedInstanceName = pairingsVector[i].getRequestedInstanceName();

                // Getting requested instance's information
                const auto& requestedInstance = _request.getInstances().at(requestedInstanceName);

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
        if (matchCount < pairingsVector.size()) return false;

        // Getting the pairings from the graph
        const auto graphPairings = graph.getLeftSidePairings();
        for (size_t i = 0; i < pairingsVector.size(); i++)
        {
         auto hostIdx = (size_t)graphPairings[i+1];
         const auto& requestedInstanceName = pairingsVector[i].getRequestedInstanceName();
         
         // Saving pairing in a map
         //printf("Pairing: %lu (%s) -> %lu\n", i, requestedInstanceName.c_str(), hostIdx);
         _pairings[requestedInstanceName] = hostIdx;
        }
         
        return true;
    }

    [[nodiscard]] __INLINE__ const std::vector<Host>& getHosts() const { return _hosts; }
    [[nodiscard]] __INLINE__ const std::map<std::string, size_t>& getPairings() const { return _pairings; }
    [[nodiscard]] __INLINE__ const Request& getRequest() const { return _request; }

    __INLINE__ nlohmann::json serialize() const
    {
        // Creating deployment JSON object
        nlohmann::json deploymentJs;

        // Getting deployment time
        deploymentJs["Deployment Start Time"] = _deployStartTime;

        // Serializing request
        deploymentJs["Request"] = _request.serialize();

        // Serializing pairings information
        size_t pairingsIndex = 0;
        for (const auto& pairing : _pairings)
        {
            deploymentJs["Pairings"][pairingsIndex]["Instance Name"] = pairing.first;
            deploymentJs["Pairings"][pairingsIndex]["Assigned Host"] = pairing.second;
            pairingsIndex++;
        }

        // Serializing host information
        for (size_t i = 0; i < _hosts.size(); i++)
            deploymentJs["Hosts"][i] = _hosts[i].serialize();

        return deploymentJs;
    }

    Deployment(const nlohmann::json& deploymentJs)
    {
        // Deserializing information
        _deployStartTime = hicr::json::getString(deploymentJs, "Deployment Start Time");
        _request = Request(hicr::json::getObject(deploymentJs, "Request"));

        _pairings.clear();
        const auto& pairingsJs = hicr::json::getArray<nlohmann::json>(deploymentJs, "Pairings");
        for (const auto& pairingJs : pairingsJs)
        {
            const auto instanceName = hicr::json::getString(pairingJs, "Instance Name");
            const auto assignedHost = hicr::json::getNumber<size_t>(pairingJs, "Assigned Host");
            _pairings[instanceName] = assignedHost;
        } 

        const auto& hostsJs = hicr::json::getArray<nlohmann::json>(deploymentJs, "Hosts");
        for (const auto& hostJs : hostsJs) _hosts.push_back(Host(hostJs));
    }

    private: 

    // Represents a pairing
    class Pairing final
    {
        public: 

        Pairing() = default;
        Pairing(const std::string& requestedInstanceName) : _requestedInstanceName(requestedInstanceName) {}

        [[nodiscard]] const std::string& getRequestedInstanceName() const { return _requestedInstanceName; }
        [[nodiscard]] const size_t& getAssignedHostIndex() const { return _assignedHostIndex; }
        void setAssignedHostIndex(const size_t& assignedHostIndex) { _assignedHostIndex = assignedHostIndex; }

        private:

        std::string _requestedInstanceName;
        size_t _assignedHostIndex;
    };

    // Time that the deployment started at the root instance
    std::string _deployStartTime;

    // Request to fulfill
    Request _request;

    // The pairings map: instance name -> HiCR instance vector entry index
    std::map<std::string, size_t> _pairings;

    // Provided hosts
    std::vector<Host> _hosts;
    
}; // class Deployment

} // namespace deployr