#pragma once

#include <hicr/core/definitions.hpp>

namespace deployr 
{

class Request final 
{
    public:

    class Machine final 
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
      };

      Machine() = default;
      ~Machine() = default;
      Machine(const nlohmann::json& machineJs)
      {
        // Parsing machine name
        _name = hicr::json::getString(machineJs, "Name");

        // Parsing the name of the function to run
        _function = hicr::json::getString(machineJs, "Function");

        // Parsing replica count
        _replicas = hicr::json::getNumber<size_t>(machineJs, "Replicas");

        // Parsing topology
        const auto topologyJs = hicr::json::getObject(machineJs, "Topology");

        // Min host capabilities
        _minHostMemoryGB = hicr::json::getNumber<size_t>(topologyJs, "Minimum Host RAM (GB)");
        _minHostProcessingUnits = hicr::json::getNumber<size_t>(topologyJs, "Minimum Host Processing Units");

        // Parsing request devices
        auto devicesJs = hicr::json::getArray<nlohmann::json>(topologyJs, "Devices");
        for (const auto& deviceJs : devicesJs) _devices.push_back(Device(deviceJs));
      }

      __INLINE__ const size_t getMinHostMemoryGB() const { return _minHostMemoryGB; }
      __INLINE__ const size_t getMinHostProcessingUnits() const { return _minHostProcessingUnits; }
      __INLINE__ const size_t getReplicas() const { return _replicas; }
      __INLINE__ const std::vector<Device>& getDevices() const { return _devices; }
      __INLINE__ const std::string& getName() const { return _name; }
      __INLINE__ const std::string& getFunction() const { return _function; }

      private: 

      std::string _name;
      std::string _function;
      size_t _replicas;
      size_t _minHostMemoryGB;
      size_t _minHostProcessingUnits;
      std::vector<Device> _devices;

    }; // class Machine

    Request() = delete;
    ~Request() = default;

    Request(const nlohmann::json& deploymentJs) 
    {
      // Parsing deployment name
      _name = hicr::json::getString(deploymentJs, "Name");

      // Parsing deployment requests
      auto requestsJs = hicr::json::getArray<nlohmann::json>(deploymentJs, "Machines");
      for (const auto& requestJs : requestsJs) _machines.push_back(Machine(requestJs));
    }

    __INLINE__ const std::vector<Machine>& getMachines() const { return _machines; }
    __INLINE__ const std::string& getName() const { return _name; }

    private: 

    std::string _name;
    std::vector<Machine> _machines;

}; // class Request

} // namespace deployr