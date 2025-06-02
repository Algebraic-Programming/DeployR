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
    * That is, whether it contains the requested devices in the host type provided
    *
    * The devices are checked in order. That is the first host device that satisfies a requested device
    * will be removed from the list when checking the next requested device.
    * 
    * @param[in] hostType The host type requested to check for
    * 
    * @return true, if this host satisfies the host type; false, otherwise.
    */
  [[nodiscard]] __INLINE__ bool checkCompatibility(const Request::HostType &hostType)
  {
    // Making a copy of the host topology.
    // Devices will be removed as we match them with the requested device
    auto hostDevices = hicr::json::getArray<nlohmann::json>(_topology, "Devices");

    ////////// Checking for requested devices
    const auto requestedDevices = hostType.getTopology();

    for (const auto &requestedDevice : requestedDevices)
    {
      const auto requestedDeviceType             = requestedDevice.getType();
      const auto requestedDeviceMemoryGB         = requestedDevice.getMinMemoryGB();
      const auto requestedDeviceComputeResources = requestedDevice.getMinComputeResources();

      // Iterating over all the host devices to see if one of them satisfies this requested device
      bool foundCompatibleDevice = false;
      for (auto hostDeviceItr = hostDevices.begin(); hostDeviceItr != hostDevices.end() && foundCompatibleDevice == false; hostDeviceItr++)
      {
        // Getting host device object
        const auto &hostDevice = hostDeviceItr.operator*();

        // Checking type
        const auto &hostDeviceType = hicr::json::getString(hostDevice, "Type");
        if (hostDeviceType == requestedDeviceType)
        {
          ///// Checking available memory
          size_t      actualHostDeviceMemoryBytes = 0;
          const auto &memorySpaces                = hicr::json::getArray<nlohmann::json>(hostDevice, "Memory Spaces");
          for (const auto &memorySpace : memorySpaces)
          {
            const auto &memorySpaceSize = hicr::json::getNumber<size_t>(memorySpace, "Size");
            actualHostDeviceMemoryBytes += memorySpaceSize;
          }

          // Calculating GB
          const size_t actualHostDeviceMemoryGB = actualHostDeviceMemoryBytes / (1024ul * 1024ul * 1024ul);

          ///// Checking requested compute resources
          const auto &computeResources                 = hicr::json::getArray<nlohmann::json>(hostDevice, "Compute Resources");
          size_t      actualHostDeviceComputeResources = computeResources.size();

          // Checking if conditions have been satisfied.
          if (actualHostDeviceComputeResources >= requestedDeviceComputeResources && actualHostDeviceMemoryGB >= requestedDeviceMemoryGB)
          {
            // Set found compatible device to true
            foundCompatibleDevice = true;

            // Deleting device to prevent it from being counted again
            hostDevices.erase(hostDeviceItr);
          }
        }
      }

      // If no host devices could satisfy the requested device, return false now
      if (foundCompatibleDevice == false) return false;
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