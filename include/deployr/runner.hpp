#pragma once

namespace deployr
{

/**
 * Describes a request for a runner: an independent, self sufficient function running on its own exclusive HiCR instance.
 */
class Runner
{
    public:

    typedef uint64_t runnerId_t;

    Runner()  = delete;
    ~Runner() = default;

    /**
    * Deserializing constructor for the Runner class
    * 
    */
    Runner(const runnerId_t id, const std::string& function, const HiCR::Instance::instanceId_t instanceId)
        : _id(id), _function(function), _instanceId(instanceId)
    { }

    /**
     * Gets the numerical id of the instance, as provided by the user
     * 
     * @return the id of the instance
     */
    [[nodiscard]] __INLINE__ const runnerId_t getId() const { return _id; }

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
    [[nodiscard]] __INLINE__ const HiCR::Instance::instanceId_t getInstanceId() const { return _instanceId; }

    private:

    /// Id assigned to this runner
    const runnerId_t _id;

    /// Function for this run to run as it is deployed
    const std::string _function;

    /// HiCR instance id assigned to this runner
    const HiCR::Instance::instanceId_t _instanceId;

}; // class Runner

} // namespace deployr