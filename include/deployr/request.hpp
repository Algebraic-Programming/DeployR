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
   * Describes a request for an instance: an independent, self sufficient function running on its own exclusive host.
   */
  class Instance
  {
    public:

    typedef uint64_t instanceId_t;

    Instance()  = delete;
    ~Instance() = default;
    
    /**
    * Deserializing constructor for the Instance class
    * 
    * @param[in] instanceJs a JSON-encoded data
    */
    Instance(const instanceId_t id, const std::string& function, const HiCR::Topology& topology)
     : _id(id), _function(function), _topology(topology)
    { }

    /**
     * Gets the numerical id of the instance, as provided by the user
     * 
     * @return the id of the instance
     */
    [[nodiscard]] __INLINE__ const instanceId_t getId() const { return _id; }

    /**
     * Gets the initial function for this instance
     * 
     * @return the initial function for this instance
     */
    [[nodiscard]] __INLINE__ const std::string &getFunction() const { return _function; }

     /**
     * Gets the topology required by this instance
     * 
     * @return the topology required by this instance
     */
    [[nodiscard]] __INLINE__ const HiCR::Topology &getTopology() const { return _topology; }

    private:

    /// Name assigned to this instance request
    const instanceId_t _id;

    /// Function for this instance to run as it is deployed
    const std::string _function;

    /// Hardware topology required by this instance
    const HiCR::Topology _topology;
  }; // class Instance

  Request()  = default;
  ~Request() = default;

  /**
   * Add an instance
   */
  __INLINE__ void addInstance(const Instance& instance) { _instances.insert({instance.getId(), instance}); }

  /**
   * Gets the instance map
   * 
   * @return the instance map
   */
  [[nodiscard]] __INLINE__ const auto &getInstances() const { return _instances; }

  private:

  /// A map of instances requested, indexed by name
  std::map<Request::Instance::instanceId_t, Instance> _instances;

}; // class Request

} // namespace deployr