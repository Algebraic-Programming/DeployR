#pragma once

#include <hicr/core/definitions.hpp>
#include <vector>
#include "runner.hpp"

namespace deployr
{

/**
 * Represents a user's requirements for a deployment. This involves the runners, their hardware requirements, and their pairing to a HiCR instance
 */
class Deployment final
{
  public:

  Deployment()  = default;
  ~Deployment() = default;

  /**
   * Add an instance
   */
  __INLINE__ void addRunner(const Runner &runner) { _runners.push_back(runner); }

  /**
   * Gets the instance map
   * 
   * @return the instance map
   */
  [[nodiscard]] __INLINE__ const auto &getRunners() const { return _runners; }

  private:

  /// A set of runners requested
  std::vector<Runner> _runners;

}; // class Deployment

} // namespace deployr