#pragma once

#include "request.hpp"

namespace deployr 
{

class Deployment final 
{
    public:

    Deployment
    (
      const std::vector<Request>& requests
    ) :
    _requests(requests)
    {

    }

    virtual ~Deployment() = default;

    private: 

    const std::vector<Request> _requests;

}; // class Request

} // namespace deployr