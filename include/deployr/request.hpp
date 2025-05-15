#pragma once

#include <hicr/core/definitions.hpp>
#include <map>

namespace deployr 
{

class Request final 
{
    public:

    class HostType final 
    {
      public:

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

        __INLINE__ const std::string& getType() const { return _type; }
        __INLINE__ const std::size_t getCount() const { return _count; }

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

      __INLINE__ const size_t getMinMemoryGB() const { return _minMemoryGB; }
      __INLINE__ const size_t getMinProcessingUnits() const { return _minProcessingUnits; }
      __INLINE__ const std::vector<Device>& getDevices() const { return _devices; }
      __INLINE__ const std::string& getName() const { return _name; }

      private: 

      std::string _name;
      size_t _minMemoryGB;
      size_t _minProcessingUnits;
      std::vector<Device> _devices;

    }; // class HostType

    class Instance
    {
      public: 

      Instance() = delete;
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

      __INLINE__ const std::string& getName() const { return _name; }
      __INLINE__ const std::string& getFunction() const { return _function; }
      __INLINE__ const std::string& getHostType() const { return _hostType; }

      private:

      std::string _name;
      std::string _function;
      std::string _hostType;
    }; // class Instance

    Request() = delete;
    ~Request() = default;

    Request(const nlohmann::json& requestJs) 
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
    }

    __INLINE__ const std::map<std::string, HostType>& getHostTypes() const { return _hostTypes; }
    __INLINE__ const std::map<std::string, Instance>& getInstances() const { return _instances; }
    __INLINE__ const std::string& getName() const { return _name; }

    private: 

    std::string _name;
    std::map<std::string, HostType> _hostTypes;
    std::map<std::string, Instance> _instances;

}; // class Request

} // namespace deployr