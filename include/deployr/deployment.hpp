#pragma once

#include "request.hpp"

namespace deployr 
{

class Deployment final 
{
    public:

    Deployment() = delete;
    ~Deployment() = default;

    Deployment(const nlohmann::json& deploymentJs) 
    {
      // Parsing deployment name
      _name = hicr::json::getString(deploymentJs, "Name");

      // Parsing deployment requests
      auto requestsJs = hicr::json::getArray<nlohmann::json>(deploymentJs, "Requests");
      for (const auto& requestJs : requestsJs) _requests.push_back(Request(requestJs));
    }

    __INLINE__ const std::vector<Request>& getRequests() const { return _requests; }
    __INLINE__ const std::string& getName() const { return _name; }

    private: 

    std::string _name;
    std::vector<Request> _requests;

}; // class Request

} // namespace deployr