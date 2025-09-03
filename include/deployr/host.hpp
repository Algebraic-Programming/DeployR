#pragma once

#include <hicr/core/definitions.hpp>
#include <hicr/core/instance.hpp>
#include "request.hpp"

namespace deployr
{

/**
 * This class represents an available hardware node (a whole host, or an SoC) that can be assigned 1:1 to a DeployR instance, to satisfy a request.
 */
class Host final
{
  public:

  Host() = delete;

  /**
    * Constructor for the Host object
    * 
    * @param[in] instance HiCR Instance corresponding to this host, as given by an instance manager
    * @param[in] topology The JSON-encoded HiCR topology detected for this host
    */
  Host(HiCR::Instance* const instance, const nlohmann::json &topology)
    : _instance(instance),
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
  [[nodiscard]] __INLINE__ bool checkCompatibility(const HiCR::Topology& topology) const
  {
    return HiCR::Topology::isSubset(_topology, topology);
  }

  /**
    * Retrieves the HiCR instance referenced by this host
    * 
    * @return A pointer to a HiCR instance
    */
  [[nodiscard]] HiCR::Instance* getInstance() const { return _instance; }

  /**
    * Retrieves the host topology
    * 
    * @return The host topolgoy
    */
  [[nodiscard]] const HiCR::Topology &getTopology() const { return _topology; }

  private:

  /// Instance Id corresponding to this host
  HiCR::Instance* const _instance;

  /// Host's actual topology, in JSON format
  const HiCR::Topology _topology;

}; // class Host

} // namespace deployr