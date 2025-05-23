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
    * @param[in] hostIndex The index of the host within the HiCR instance list
    * @param[in] topology The JSON-encoded HiCR topology detected for this host
    */
  Host(const size_t hostIndex, const nlohmann::json &topology)
    : _hostIndex(hostIndex),
      _topology(topology)
  {}
  ~Host() = default;

  /**
    * Checks whether this host satisfied a certain host type.
    * That is, whether it contains the minimuim memory and processing units required, and that all the devices
    * enumerated within the host type are present in this device
    * 
    * @param[in] hostType The host type requested to check for
    * 
    * @return true, if this host satisfies the host type; false, otherwise.
    */
  [[nodiscard]] __INLINE__ bool checkCompatibility(const Request::HostType &hostType)
  {
    ////////// Checking whether the _topology contains the minimum host memory
    const auto minHostMemoryGB = hostType.getMinMemoryGB();

    // Getting topology's devices
    const auto &devices = hicr::json::getArray<nlohmann::json>(_topology, "Devices");

    // Looking for NUMA Domain device to add up to the actual memory
    size_t actualHostMemoryBytes = 0;
    for (const auto &device : devices)
    {
      const auto &deviceType = hicr::json::getString(device, "Type");
      if (deviceType == "NUMA Domain")
      {
        const auto &memorySpaces = hicr::json::getArray<nlohmann::json>(device, "Memory Spaces");
        for (const auto &memorySpace : memorySpaces)
        {
          const auto &memorySpaceType = hicr::json::getString(memorySpace, "Type");
          if (memorySpaceType == "RAM")
          {
            const auto &memorySpaceSize = hicr::json::getNumber<size_t>(memorySpace, "Size");
            actualHostMemoryBytes       = memorySpaceSize;
          }
        }
      }
    }

    // Calculating GB
    const size_t actualHostMemoryGB = actualHostMemoryBytes / (1024ul * 1024ul * 1024ul);

    // Returning false if not enough host memory found
    if (actualHostMemoryGB < minHostMemoryGB) return false;

    ////////// Checking whether the _topology contains the minimum processing units
    const auto minHostProcessingUnits = hostType.getMinProcessingUnits();

    // Looking for NUMA Domain device to add up the number of processing units
    size_t actualHostProcessingUnits = 0;
    for (const auto &device : devices)
    {
      const auto &deviceType = hicr::json::getString(device, "Type");
      if (deviceType == "NUMA Domain")
      {
        const auto &computeResources = hicr::json::getArray<nlohmann::json>(device, "Compute Resources");
        for (const auto &computeResource : computeResources)
        {
          const auto &computeResourceType = hicr::json::getString(computeResource, "Type");
          if (computeResourceType == "Processing Unit") actualHostProcessingUnits++;
        }
      }
    }

    // Returning false if not enough processing units found
    if (actualHostProcessingUnits < minHostProcessingUnits) return false;
    // printf("Found %luGB - %lu PUs\n", actualHostMemoryGB, actualHostProcessingUnits);

    ////////// Checking for requested devices
    const auto requestedDevices = hostType.getDevices();

    for (const auto &requestedDevice : requestedDevices)
    {
      const auto requestedDeviceType  = requestedDevice.getType();
      const auto requestedDeviceCount = requestedDevice.getCount();

      // Looking for NUMA Domain device to add up the number of processing units
      size_t actualDeviceCount = 0;
      for (const auto &device : devices)
      {
        const auto &deviceType = hicr::json::getString(device, "Type");

        // printf("Comparing %s to %s\n", device->getType().c_str(), requestedDeviceType.c_str());
        if (deviceType == requestedDeviceType) actualDeviceCount++;
      }

      // printf("Actual device Count: %lu\n", actualDeviceCount);
      // Failing if the require device count hasn't been met
      if (actualDeviceCount < requestedDeviceCount) return false;
    }

    // All requirements have been met, returning true
    //printf("Requirements met\n");
    return true;
  }

  /**
    * Retrieves the host index
    * 
    * @return The host index
    */
  [[nodiscard]] const size_t getHostIndex() const { return _hostIndex; }

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
    hostJs["Host Index"] = _hostIndex;

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
    _hostIndex = hicr::json::getNumber<size_t>(hostJs, "Host Index");
    _topology  = hicr::json::getObject(hostJs, "Topology");
  }

  private:

  /// Index of the corresponding host within the instance manager's instance vector
  size_t _hostIndex;

  /// Host's actual topology, in JSON format
  nlohmann::json _topology;

}; // class Host

} // namespace deployr