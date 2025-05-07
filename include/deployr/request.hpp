#pragma once

#include <vector>
#include <string>

namespace deployr 
{

class Request final 
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

    Request() = delete;
    ~Request() = default;
    Request(const nlohmann::json& requestJs)
    {
      // Parsing deployment name
      _name = hicr::json::getString(requestJs, "Name");

      // Parsing replica count
      _replicas = hicr::json::getNumber<size_t>(requestJs, "Replicas");

      // Parsing topology
      const auto topologyJs = hicr::json::getObject(requestJs, "Topology");

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

    private: 

    std::string _name;
    size_t _replicas;
    size_t _minHostMemoryGB;
    size_t _minHostProcessingUnits;
    std::vector<Device> _devices;

}; // class Request

} // namespace deployr