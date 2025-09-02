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

  /**
   * Constructor for a deployment
   * 
   * @param[in] request A user-provided request containing the requiments for this deployment.
   * 
   * The start time of deployment is taken at this time.
   */
  Deployment(Request request)
    : _request(request)
  {
    // Setting start time at the creation of this object
    _deployStartTime = getCurrentDateTime();
  };
  ~Deployment() = default;

  /**
   * Adds an available host (with its own topology) for pairing
   * 
   * @param[in] host The host to add
   */
  __INLINE__ void addHost(const Host &host) { _hosts.push_back(host); }

  /**
   * Performs a matching between the requested instances and the available hosts
   * 
   * This implementation uses the Hopcroft-Karp algorithm to find a matching of all requested instances to a host.
   * 
   * @return True, if the matching was possible. False, otherwise.
   */
  [[nodiscard]] __INLINE__ bool performMatching()
  {
    // Creating flat pairings vector
    std::vector<Pairing> pairingsVector;

    // Creating one deployment instance per requested instance
    for (const auto &requestedInstance : _request.getInstances()) pairingsVector.push_back(Pairing(requestedInstance.second.getId()));

    // Building the matching graph
    theAlgorithms::graph::HKGraph graph(pairingsVector.size(), _hosts.size());
    for (size_t i = 0; i < pairingsVector.size(); i++)
      for (size_t j = 0; j < _hosts.size(); j++)
      {
        // Getting requested instance's name
        const auto &requestedInstanceName = pairingsVector[i].getRequestedInstanceId();

        // Getting requested instance's information
        const auto &requestedInstance = _request.getInstances().at(requestedInstanceName);

        // Getting associated host type name
        const auto &requestedTopology = requestedInstance.getTopology();

        // Checking if the requested host type is compatible with the current host.
        // If so, add an edge to the graph
        if (_hosts[j].checkCompatibility(requestedTopology)) graph.addEdge(i, j);
      }

    //  Finding out if a proper matching exists
    auto matchCount = (size_t)graph.hopcroftKarpAlgorithm();

    // If the number of matchings is smaller than requested, return false
    if (matchCount < pairingsVector.size()) return false;

    // Getting the pairings from the graph
    const auto graphPairings = graph.getLeftSidePairings();
    for (size_t i = 0; i < pairingsVector.size(); i++)
    {
      auto        hostIdx               = (size_t)graphPairings[i + 1];
      const auto &requestedInstanceId = pairingsVector[i].getRequestedInstanceId();

      // Saving pairing in a map
      //printf("Pairing: %lu (%s) -> %lu\n", i, requestedInstanceName.c_str(), hostIdx);
      _pairings[requestedInstanceId] = hostIdx;
    }

    return true;
  }

  /**
   * Gets the available hosts in this deployment
   * 
   * @return A list of the added hosts
   */
  [[nodiscard]] __INLINE__ const std::vector<Host> &getHosts() const { return _hosts; }

  /**
   * Gets the instance <-> host pairings
   * 
   * @return A map that links each instance by name to its assigned host index
   */
  [[nodiscard]] __INLINE__ const auto &getPairings() const { return _pairings; }

  /**
   * Gets the underlying request used to generate this deployment
   * 
   * @return The request object
   */
  [[nodiscard]] __INLINE__ const Request &getRequest() const { return _request; }

  private:

  /// Represents an instance->host pairing
  class Pairing final
  {
    public:

    Pairing() = default;

    /**
     * Constructor that takes the requested instance id
     * 
     * @param[in] requestedInstanceId The id of the instance for this pairing
     */
    Pairing(const Request::Instance::instanceId_t requestedInstanceId)
      : _requestedInstanceId(requestedInstanceId)
    {}

    /**
     * Function to get the name of the pairing's requested instance
     * 
     * @return The instance name
     */
    [[nodiscard]] const Request::Instance::instanceId_t &getRequestedInstanceId() const { return _requestedInstanceId; }

    /**
     * Function to get the index host assigned to the instance
     * 
     * @return The host index
     */
    [[nodiscard]] const size_t &getAssignedHostIndex() const { return _assignedHostIndex; }

    /**
     * Function to set the index host assigned to the instance
     * 
     * @param[in] assignedHostIndex The index to the host assigned to this instance
     */
    void setAssignedHostIndex(const size_t &assignedHostIndex) { _assignedHostIndex = assignedHostIndex; }

    private:

    /// The name of the requested instance for this pairing
    Request::Instance::instanceId_t _requestedInstanceId;

    /// The index of the host assigned to the instance
    size_t _assignedHostIndex;
  };

  /// Time that the deployment started at the root instance
  std::string _deployStartTime;

  /// Request to fulfill
  Request _request;

  /// The pairings map: instance name -> HiCR instance vector entry index
  std::map<Request::Instance::instanceId_t, size_t> _pairings;

  /// Provided hosts
  std::vector<Host> _hosts;

}; // class Deployment

} // namespace deployr