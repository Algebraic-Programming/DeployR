#pragma once

#include <hicr/core/definitions.hpp>
#include <hopcroft_karp.hpp>
#include "request.hpp"
#include "host.hpp"
#include "common.hpp"

namespace deployr 
{

class Deployment final 
{
    public:

    // Represents a pairing
    class Pairing final
    {
        public: 

        Pairing() = default;
        Pairing(const std::string& requestedInstanceName) : _requestedInstanceName(requestedInstanceName) {}

        const std::string& getRequestedInstanceName() const { return _requestedInstanceName; }
        const size_t& getAssignedHostIndex() const { return _assignedHostIndex; }
        void setAssignedHostIndex(const size_t& assignedHostIndex) { _assignedHostIndex = assignedHostIndex; }

        __INLINE__ nlohmann::json serialize() const
        {
            // Creating deployement instance's JSON object
            nlohmann::json instanceJs;
    
            // Getting deployment time
            instanceJs["Requested Instance Name"] = _requestedInstanceName;

            // Serializing Host information
            instanceJs["Assigned Host Index"] = _assignedHostIndex;

            return instanceJs;
        }

        Pairing(const nlohmann::json& pairingJs)
        {
            // Deserializing pairing information
            _requestedInstanceName = hicr::json::getString(pairingJs, "Requested Instance Name");
            _assignedHostIndex = hicr::json::getNumber<size_t>(pairingJs, "Assigned Host Index");
        }

        private:

        std::string _requestedInstanceName;
        size_t _assignedHostIndex;
    };

    Deployment() = default;
    Deployment(Request request) : _request(request)
    {
        // Setting start time at the creation of this object
        _deployStartTime = getCurrentDateTime();

        // Creating one deployment instance per requested instance
        for (const auto& requestedInstance : _request.getInstances())
            _pairings.push_back(Pairing(requestedInstance.second.getName()));
    };
    ~Deployment() = default;

    __INLINE__ void addHost(const Host& host) { _hosts.push_back(host); }

    __INLINE__ bool performMatching()
    {
        // Building the matching graph
        theAlgorithms::graph::HKGraph graph(_pairings.size(), _hosts.size());
        for (size_t i = 0; i < _pairings.size(); i++)
            for (size_t j = 0; j < _hosts.size(); j++)
            {
                // Getting requested instance's name
                const auto& requestedInstanceName = _pairings[i].getRequestedInstanceName();

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
        if (matchCount < _pairings.size()) return false;

        // Getting the pairings from the graph
        const auto graphPairings = graph.getLeftSidePairings();
        for (size_t i = 0; i < _pairings.size(); i++)
        {
         auto hostIdx = (size_t)graphPairings[i+1];
         //printf("Pairing: %lu -> %lu\n", i, hostIdx);
         _pairings[i].setAssignedHostIndex(hostIdx);
        }
         
        return true;
    }

    __INLINE__ const std::vector<Host>& getHosts() const { return _hosts; }
    __INLINE__ const std::vector<Pairing>& getPairings() const { return _pairings; }
    __INLINE__ const Request& getRequest() const { return _request; }

    __INLINE__ nlohmann::json serialize() const
    {
        // Creating deployment JSON object
        nlohmann::json deploymentJs;

        // Getting deployment time
        deploymentJs["Deployment Start Time"] = _deployStartTime;

        // Serializing request
        deploymentJs["Request"] = _request.serialize();

        // Serializing pairings information
        for (size_t i = 0; i < _pairings.size(); i++)
            deploymentJs["Pairings"][i] = _pairings[i].serialize();

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

        const auto& pairingsJs = hicr::json::getArray<nlohmann::json>(deploymentJs, "Pairings");
        for (const auto& pairingJs : pairingsJs) _pairings.push_back(Pairing(pairingJs));

        const auto& hostsJs = hicr::json::getArray<nlohmann::json>(deploymentJs, "Hosts");
        for (const auto& hostJs : hostsJs) _hosts.push_back(Host(hostJs));
    }

    private: 

    // Time that the deployment started at the root instance
    std::string _deployStartTime;

    // Request to fulfill
    Request _request;

    // The pairings vector
    std::vector<Pairing> _pairings;

    // Provided hosts
    std::vector<Host> _hosts;
    
}; // class Deployment

} // namespace deployr