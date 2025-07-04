#pragma once

#include <hicr/core/definitions.hpp>
#include "request.hpp"

namespace deployr
{

/**
 * This class represents an available hardware node (a whole host, or an SoC) that can be assigned 1:1 to a DeployR instance, to satisfy a request.
 */
class Host final
{
  public:

  Host() = default;

  /**
    * Constructor for the Host object
    * 
    * @param[in] instanceId Instance Id corresponding to this host, as given by an instance manager
    * @param[in] topology The JSON-encoded HiCR topology detected for this host
    */
  Host(const size_t instanceId, const nlohmann::json &topology)
    : _instanceId(instanceId),
      _topology(topology)
  {}
  ~Host() = default;

  /**
    * Checks whether this host satisfied a certain host type.
    * That is, whether it contains the requested devices in the host type provided
    *
    * The devices are checked in order. That is the first host device that satisfies a requested device
    * will be removed from the list when checking the next requested device.
    * 
    * @param[in] hostType The host type requested to check for
    * 
    * @return true, if this host satisfies the host type; false, otherwise.
    */
  [[nodiscard]] __INLINE__ bool checkCompatibility(const Request::HostType &hostType)
  {
    ////////// Checking for requested devices
    const auto hostTopology = HiCR::Topology(hostType.getTopology());
    const auto requestedTopologyJs = hostType.getTopology();

    //printf("Requirements met\n");
    return true;
  }

  /**
    * Retrieves the host index
    * 
    * @return The host index
    */
  [[nodiscard]] const size_t getInstanceId() const { return _instanceId; }

  /**
    * Retrieves the host topology
    * 
    * @return The host topolgoy
    */
  [[nodiscard]] const nlohmann::json &getTopology() const { return _topology; }

  /**
    * Serializes the host information to be sent to remote instances and deserialized there
    * 
    * @return A JSON-encoded details of the host
    */
  [[nodiscard]] __INLINE__ nlohmann::json serialize() const
  {
    // Creating host JSON object
    nlohmann::json hostJs;

    // Getting deployment time
    hostJs["Instance Id"] = _instanceId;

    // Serializing request
    hostJs["Topology"] = _topology;

    return hostJs;
  }

  /**
    * Deserializing constructor
    * 
    * @param[in] hostJs A JSON-encoded details of the host
    */
  Host(const nlohmann::json &hostJs)
  {
    // Deserializing information
    _instanceId = hicr::json::getNumber<size_t>(hostJs, "Instance Id");
    _topology  = hicr::json::getObject(hostJs, "Topology");
  }

  private:

  /// Instance Id corresponding to this host
  size_t _instanceId;

  /// Host's actual topology, in JSON format
  nlohmann::json _topology;

}; // class Host

} // namespace deployr