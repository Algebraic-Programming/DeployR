
#include <hicr/core/exceptions.hpp>
#include <hicr/core/definitions.hpp>
#include "engine.hpp"

#ifdef _DEPLOYR_DISTRIBUTED_ENGINE_MPI
  #include "engines/mpi.hpp"
#endif

#ifdef _DEPLOYR_DISTRIBUTED_ENGINE_LOCAL
  #include "engines/local.hpp"
#endif

namespace deployr
{

class DeployR final
{
 public:

  DeployR()
  {

  }

  ~DeployR() = default;

  __INLINE__ void initialize(int* pargc, char*** pargv)
  {
      //// Instantiating distributed execution engine
      #ifdef _DEPLOYR_DISTRIBUTED_ENGINE_MPI
      _engine = std::make_unique<engine::MPI>(pargc, pargv);
      #endif

      #ifdef _DEPLOYR_DISTRIBUTED_ENGINE_LOCAL
      _engine = std::make_unique<engine::Local>(pargc, pargv);
      #endif
  }

private:

std::unique_ptr<Engine> _engine;

}; // class DeployR

} // namespace deployr