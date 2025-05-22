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

      [[nodiscard]] __INLINE__ const std::string& getName() const { return _name; }
      [[nodiscard]] __INLINE__ const std::vector<std::string>& getProducers() const { return _producers; }
      [[nodiscard]] __INLINE__ const std::string& getConsumer() const { return _consumer; }
      [[nodiscard]] __INLINE__ const size_t getBufferCapacity() const { return _bufferCapacity; }
      [[nodiscard]] __INLINE__ const size_t getBufferSize() const { return _bufferSize; }

      private:

      std::string _name;
      std::vector<std::string> _producers;
      std::string _consumer;
      size_t _bufferCapacity;
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
        Device(const nlohmann::json& deviceJs)
        {
          // Parsing device type
          _type = hicr::json::getString(deviceJs, "Type");

          // Parsing device count
          _count = hicr::json::getNumber<size_t>(deviceJs, "Count");
        }

        [[nodiscard]] __INLINE__ const std::string& getType() const { return _type; }
        [[nodiscard]] __INLINE__ const std::size_t getCount() const { return _count; }

        private:

        std::string _type;
        size_t _count;
      }; // class Device

      HostType() = default;
      ~HostType() = default;
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

      [[nodiscard]] __INLINE__ const size_t getMinMemoryGB() const { return _minMemoryGB; }
      [[nodiscard]] __INLINE__ const size_t getMinProcessingUnits() const { return _minProcessingUnits; }
      [[nodiscard]] __INLINE__ const std::vector<Device>& getDevices() const { return _devices; }
      [[nodiscard]] __INLINE__ const std::string& getName() const { return _name; }

      private: 

      std::string _name;
      size_t _minMemoryGB;
      size_t _minProcessingUnits;
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
      Instance(const nlohmann::json& instanceJs)
      {
        // Parsing hostType name
        _name = hicr::json::getString(instanceJs, "Name");

        // Parsing the host type requested
        _hostType = hicr::json::getString(instanceJs, "Host Type");

        // Parsing the name of the function to run
        _function = hicr::json::getString(instanceJs, "Function");
      }

      [[nodiscard]] __INLINE__ const std::string& getName() const { return _name; }
      [[nodiscard]] __INLINE__ const std::string& getFunction() const { return _function; }
      [[nodiscard]] __INLINE__ const std::string& getHostType() const { return _hostType; }

      private:

      std::string _name;
      std::string _function;
      std::string _hostType;
    }; // class Instance

    Request() = default;
    ~Request() = default;

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

    [[nodiscard]] __INLINE__ const std::map<std::string, HostType>& getHostTypes() const { return _hostTypes; }
    [[nodiscard]] __INLINE__ const std::map<std::string, Instance>& getInstances() const { return _instances; }
    [[nodiscard]] __INLINE__ const std::vector<Channel>& getChannels() const { return _channels; }
    [[nodiscard]] __INLINE__ const std::string& getName() const { return _name; }

    [[nodiscard]] __INLINE__ nlohmann::json serialize() const
    {
        // Since this is a static object, simply return originating JSON
        return _requestJs;
    }

    private: 

    nlohmann::json _requestJs;
    std::string _name;
    std::map<std::string, HostType> _hostTypes;
    std::map<std::string, Instance> _instances;
    std::vector<Channel> _channels;

}; // class Request

} // namespace deployr