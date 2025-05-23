#pragma once

#include <hicr/core/definitions.hpp>
#include <map>
#include <vector>
#include <algorithm>

namespace deployr 
{

/**
 * A request object, representing a user's requirements for a deployment. This involves the instances, their hardware requirements, and the channels to create among them.
 */
class Request final 
{
    public:

    /**
     * Represents the request for the creation of a channel during deployment. It indicates the buffer size and which will be the producer instances and which will be the consumer.
     */
    class Channel
    {
      public: 

      Channel() = delete;
      ~Channel() = default;

      /**
       * Deserializing constructor for a channel request
       * 
       * @param[in] channelJs A JSON-encoded channel request information
       */
      Channel(const nlohmann::json& channelJs)
      {
        // Parsing channel name
        _name = hicr::json::getString(channelJs, "Name");

        // Parsing producers
        _producers = hicr::json::getArray<std::string>(channelJs, "Producers");

        // Parsing consumer
        _consumer = hicr::json::getString(channelJs, "Consumer");

        // Checking if consumer is not part of the producer list
        if (std::find(_producers.begin(), _producers.end(), _consumer) != _producers.end()) HICR_THROW_LOGIC("Channel '%s' defines '%s' as both consumer and producer. This is not supported\n", _name.c_str(), _consumer.c_str());

        // Parsing buffer size (in tokens)
        _bufferCapacity = hicr::json::getNumber<size_t>(channelJs, "Buffer Capacity (Tokens)");

        // Parsing buffer size (in tokens)
        _bufferSize = hicr::json::getNumber<size_t>(channelJs, "Buffer Size (Bytes)");
      }

      /**
       * Gets the channel name
       * 
       * @return the channel name
       */
      [[nodiscard]] __INLINE__ const std::string& getName() const { return _name; }

      /**
       * Gets the list of channel producers
       * 
       * @return the list of channel producers
       */
      [[nodiscard]] __INLINE__ const std::vector<std::string>& getProducers() const { return _producers; }

      /**
       * Gets the channel consumer
       * 
       * @return the channel consumer
       */
      [[nodiscard]] __INLINE__ const std::string& getConsumer() const { return _consumer; }

      /**
       * Gets the buffer capacity
       * 
       * @return the buffer capacity
       */
      [[nodiscard]] __INLINE__ const size_t getBufferCapacity() const { return _bufferCapacity; }

      /**
       * Gets the buffer size
       * 
       * @return the buffer size (bytes)
       */
      [[nodiscard]] __INLINE__ const size_t getBufferSize() const { return _bufferSize; }

      private:

      /// The channel name. This value will be used by the user to retrieve the corresponding channel object
      std::string _name;

      /// List of producers for this channel. Identifies instances by their name
      std::vector<std::string> _producers;

      /// Consumer for this channel. Identifies an instance by its name
      std::string _consumer;

      /// Buffer capacity requested for this channel. It indicates how many messages can be stored in buffer at any given time
      size_t _bufferCapacity;

      /// Buffer size (in bytes) requested for this channel. It indicates how many bytes can be stored in the buffer
      size_t _bufferSize;
    }; // class Channel

    /**
     * Represents a template for the hardware components of a given Host. The request's instances will point to one such template to indicate the minimum hardware requirements they need for deployment.
     */
    class HostType final 
    {
      public:

      /**
       * Represents a device type to be contained in this host type, and how many of these devices should be present. It can indicate a certain GPU/NPU device, as well as CPU NUMA domains.
       */
      class Device
      {
        public: 

        Device() = delete;
        ~Device() = default;

        /**
         * Deserializing constructor for the device request
         * 
         * @param[in] deviceJs A JSON-encoded device request information
         */
        Device(const nlohmann::json& deviceJs)
        {
          // Parsing device type
          _type = hicr::json::getString(deviceJs, "Type");

          // Parsing device count
          _count = hicr::json::getNumber<size_t>(deviceJs, "Count");
        }

        /**
         * Gets the device type
         * 
         * @return The device type
         */
        [[nodiscard]] __INLINE__ const std::string& getType() const { return _type; }

        /**
         * Gets the device count. I.e., the number of instances of the devices to look for
         * 
         * @return The device count required
         */
        [[nodiscard]] __INLINE__ const std::size_t getCount() const { return _count; }

        private:

        /// The type of the device, to use to check whether the host contains such a device
        std::string _type;

        /// The number of such devices to look for
        size_t _count;
      }; // class Device

      HostType() = default;
      ~HostType() = default;

      /**
       * Deserializing constructor for the hostType class
       * 
       * @param[in] hostTypeJs a JSON-encoded data
       */
      HostType(const nlohmann::json& hostTypeJs)
      {
        // Parsing hostType name
        _name = hicr::json::getString(hostTypeJs, "Name");

        // Parsing topology
        const auto topologyJs = hicr::json::getObject(hostTypeJs, "Topology");

        // Min host capabilities
        _minMemoryGB = hicr::json::getNumber<size_t>(topologyJs, "Minimum Host RAM (GB)");
        _minProcessingUnits = hicr::json::getNumber<size_t>(topologyJs, "Minimum Host Processing Units");

        // Parsing request devices
        auto devicesJs = hicr::json::getArray<nlohmann::json>(topologyJs, "Devices");
        for (const auto& deviceJs : devicesJs) _devices.push_back(Device(deviceJs));
      }

      /**
       * Gets the minimum memory (in GB) required by this host type
       * 
       * @return the minimum memory (in GB) required by this host type
       */
      [[nodiscard]] __INLINE__ const size_t getMinMemoryGB() const { return _minMemoryGB; }

      /**
       * Gets the minimum processing units required by this host type
       * 
       * @return the minimum processing units required by this host type
       */
      [[nodiscard]] __INLINE__ const size_t getMinProcessingUnits() const { return _minProcessingUnits; }

      /**
       * Gets the devices required by this host type
       * 
       * @return the devices required by this host type
       */
      [[nodiscard]] __INLINE__ const std::vector<Device>& getDevices() const { return _devices; }

      /**
       * Gets the host type name. 
       * 
       * @return the host type name
       */
      [[nodiscard]] __INLINE__ const std::string& getName() const { return _name; }

      private: 

      /// The name associated to this host type. This value will be used to link instance requirements to corresponding host type
      std::string _name;

      /// Minimum memory (GB) required by this host type
      size_t _minMemoryGB;

      /// Minimum processing units required by this host type
      size_t _minProcessingUnits;

      /// Set of devices required by this host type
      std::vector<Device> _devices;

    }; // class HostType

    /**
     * Describes a request for an instance: an independent, self sufficient function running on its own exclusive host.
     */
    class Instance
    {
      public: 

      Instance() = default;
      ~Instance() = default;

      /**
      * Deserializing constructor for the Instance class
      * 
      * @param[in] instanceJs a JSON-encoded data
      */
      Instance(const nlohmann::json& instanceJs)
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
      [[nodiscard]] __INLINE__ const std::string& getName() const { return _name; }

      /**
       * Gets the initial function for this instance
       * 
       * @return the initial function for this instance
       */
      [[nodiscard]] __INLINE__ const std::string& getFunction() const { return _function; }

      /**
       * Gets the host type required for this instance
       * 
       * @return the host type required for this instance
       */
      [[nodiscard]] __INLINE__ const std::string& getHostType() const { return _hostType; }

      private:

      /// Name assigned to this instance request
      std::string _name;

      /// Function for this instance to run as it is deployed
      std::string _function;

      /// Host type name that describes the hardware topology required by this instance request
      std::string _hostType;
    }; // class Instance

    Request() = default;
    ~Request() = default;

      /**
      * Deserializing constructor for the Request class
      * 
      * @param[in] requestJs a JSON-encoded data
      */
    Request(const nlohmann::json& requestJs) : _requestJs(requestJs)
    {
      // Parsing name
      _name = hicr::json::getString(requestJs, "Name");

      // Getting host types entry
      const auto& hostTypesJs = hicr::json::getArray<nlohmann::json>(requestJs, "Host Types");

      // Parsing individual host types
      for (const auto& hostTypeJs : hostTypesJs)
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
      const auto& instancesJs = hicr::json::getArray<nlohmann::json>(requestJs, "Instances");

      // Parsing individual instances
      for (const auto& instanceJs : instancesJs)
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

      // Getting channels entry
      const auto& channelsJs = hicr::json::getArray<nlohmann::json>(requestJs, "Channels");

      // Parsing individual channels
      for (const auto& channelJs : channelsJs)
      {
        // Creating instance object
        const auto channel = Channel(channelJs);

        // If not repeated, push the new host type
        _channels.push_back(channel);
      }
    }

    /**
     * Gets the host type map
     * 
     * @return the host type map
     */
    [[nodiscard]] __INLINE__ const std::map<std::string, HostType>& getHostTypes() const { return _hostTypes; }

    /**
     * Gets the instance map
     * 
     * @return the instance map
     */
    [[nodiscard]] __INLINE__ const std::map<std::string, Instance>& getInstances() const { return _instances; }

    /**
     * Gets the channel vector
     * 
     * @return the channel vector
     */
    [[nodiscard]] __INLINE__ const std::vector<Channel>& getChannels() const { return _channels; }

    /**
     * Gets the request name
     * 
     * @return the request name
     */
    [[nodiscard]] __INLINE__ const std::string& getName() const { return _name; }

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

    /// An array of channels that are required to be created upon deployment
    std::vector<Channel> _channels;

}; // class Request

} // namespace deployr