#pragma once

#include <hicr/core/definitions.hpp>
#include <map>
#include <vector>
#include <algorithm>

namespace deployr
{

/**
 * A request object, representing a user's requirements for a deployment. This involves the instances, and their hardware requirements
 */
class Request final
{
  public:

  /**
   * Represents a template for the hardware components of a given Host. The request's instances will point to one such template to indicate the minimum hardware requirements they need for deployment.
   */
  class HostType final
  {
    public:

    HostType()  = default;
    ~HostType() = default;

    /**
     * Deserializing constructor for the hostType class
     * 
     * @param[in] hostTypeJs a JSON-encoded data
     */
    HostType(const nlohmann::json &hostTypeJs)
    {
      // Parsing hostType name
      _name = hicr::json::getString(hostTypeJs, "Name");

      // Parsing request devices
      _topology = hicr::json::getObject(hostTypeJs, "Topology");
    }

    /**
     * Gets the topology required by this host type
     * 
     * @return the topology required by this host type
     */
    [[nodiscard]] __INLINE__ const nlohmann::json &getTopology() const { return _topology; }

    /**
     * Gets the host type name. 
     * 
     * @return the host type name
     */
    [[nodiscard]] __INLINE__ const std::string &getName() const { return _name; }

    private:

    /// The name associated to this host type. This value will be used to link instance requirements to corresponding host type
    std::string _name;

    /// Set of topology required by this host type
    nlohmann::json _topology;

  }; // class HostType

  /**
   * Describes a request for an instance: an independent, self sufficient function running on its own exclusive host.
   */
  class Instance
  {
    public:

    Instance()  = default;
    ~Instance() = default;

    /**
    * Deserializing constructor for the Instance class
    * 
    * @param[in] instanceJs a JSON-encoded data
    */
    Instance(const nlohmann::json &instanceJs)
    {
      // Parsing hostType name
      _name = hicr::json::getString(instanceJs, "Name");

      // Parsing the host type requested
      _hostType = hicr::json::getString(instanceJs, "Host Type");

      // Parsing the name of the function to run
      _function = hicr::json::getString(instanceJs, "Function");
    }

    /**
     * Gets the name of the instance
     * 
     * @return the name of the instance
     */
    [[nodiscard]] __INLINE__ const std::string &getName() const { return _name; }

    /**
     * Gets the initial function for this instance
     * 
     * @return the initial function for this instance
     */
    [[nodiscard]] __INLINE__ const std::string &getFunction() const { return _function; }

    /**
     * Gets the host type required for this instance
     * 
     * @return the host type required for this instance
     */
    [[nodiscard]] __INLINE__ const std::string &getHostType() const { return _hostType; }

    private:

    /// Name assigned to this instance request
    std::string _name;

    /// Function for this instance to run as it is deployed
    std::string _function;

    /// Host type name that describes the hardware topology required by this instance request
    std::string _hostType;
  }; // class Instance

  Request()  = default;
  ~Request() = default;

  /**
    * Deserializing constructor for the Request class
    * 
    * @param[in] requestJs a JSON-encoded data
    */
  Request(const nlohmann::json &requestJs)
    : _requestJs(requestJs)
  {
    // Parsing name
    _name = hicr::json::getString(requestJs, "Name");

    // Getting host types entry
    const auto &hostTypesJs = hicr::json::getArray<nlohmann::json>(requestJs, "Host Types");

    // Parsing individual host types
    for (const auto &hostTypeJs : hostTypesJs)
    {
      // Creating host type object
      const auto hostType = HostType(hostTypeJs);

      // Getting host type name
      const auto hostTypeName = hostType.getName();

      // Checking if the same host type name was already provided
      if (_hostTypes.contains(hostTypeName)) HICR_THROW_LOGIC("Repeated host type provided: %s\n", hostTypeName.c_str());

      // If not repeated, push the new host type
      _hostTypes.insert({hostTypeName, hostType});
    }

    // Getting instances  entry
    const auto &instancesJs = hicr::json::getArray<nlohmann::json>(requestJs, "Instances");

    // Parsing individual instances
    for (const auto &instanceJs : instancesJs)
    {
      // Creating instance object
      const auto instance = Instance(instanceJs);

      // Getting instance name
      const auto instanceName = instance.getName();

      // Getting associated host type
      const auto hostTypeName = instance.getHostType();

      // Checking if the same host type name was already provided
      if (_instances.contains(instanceName)) HICR_THROW_LOGIC("Repeated instance name provided: '%s'\n", instanceName.c_str());

      // Checking if the requested host type was defined
      if (_hostTypes.contains(hostTypeName) == false) HICR_THROW_LOGIC("Instance '%s' requested undefined host type: '%s'\n", instanceName.c_str(), hostTypeName.c_str());

      // If not repeated, push the new host type
      _instances.insert({instanceName, instance});
    }
  }

  /**
   * Gets the host type map
   * 
   * @return the host type map
   */
  [[nodiscard]] __INLINE__ const std::map<std::string, HostType> &getHostTypes() const { return _hostTypes; }

  /**
   * Gets the instance map
   * 
   * @return the instance map
   */
  [[nodiscard]] __INLINE__ const std::map<std::string, Instance> &getInstances() const { return _instances; }

  /**
   * Gets the request name
   * 
   * @return the request name
   */
  [[nodiscard]] __INLINE__ const std::string &getName() const { return _name; }

  /**
   * Serialization function for this request
   * 
   * @return The original JSON-encoded data used to create this request
   */
  [[nodiscard]] __INLINE__ nlohmann::json serialize() const
  {
    // Since this is a static object, simply return originating JSON
    return _requestJs;
  }

  private:

  /// The original JSON-encoded request used to create this object
  nlohmann::json _requestJs;

  /// The name of the request
  std::string _name;

  /// A map of host types associated to this request, indexed by name
  std::map<std::string, HostType> _hostTypes;

  /// A map of instances requested, indexed by name
  std::map<std::string, Instance> _instances;

}; // class Request

} // namespace deployr