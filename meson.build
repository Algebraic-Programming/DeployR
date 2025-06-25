project('DeployR', [ 'cpp', 'c'],
  subproject_dir : 'extern',
  default_options : [
  'cpp_std=c++20',
  'buildtype=release'
  ]
)

####### Storage for DeployR dependencies

deployrDependencies = [ ]

##### Getting selected distributed engine

engine = get_option('engine')

# If, selected, adding the dependency for the distributed engine

if engine == 'local'
  DeployRDistributedCppFlag = '-D_DEPLOYR_DISTRIBUTED_ENGINE_LOCAL'

  additionalHiCRBackends = [ ]
  additionalHiCRFrontends = [ ]
endif

if engine == 'mpi'
  DeployRDistributedCppFlag = '-D_DEPLOYR_DISTRIBUTED_ENGINE_MPI'
  mpirunExecutable = find_program('mpirun', '/usr/bin/mpirun', '/usr/local/bin/mpirun', required : true)

  additionalHiCRBackends = [ 'mpi' ]
  additionalHiCRFrontends = [ ]
endif

if engine == 'lpf'
  DeployRDistributedCppFlag = '-D_DEPLOYR_DISTRIBUTED_ENGINE_LPF'

  additionalHiCRBackends = [ 'mpi', 'lpf' ]
  additionalHiCRFrontends = [ ]
endif

if engine == 'cloudr-mpi'
  CloudRProject = subproject('cloudr', required: true, default_options: [  ])
  CloudRBuildDep = CloudRProject.get_variable('CloudRBuildDep')
  deployrDependencies += CloudRBuildDep
  DeployRDistributedCppFlag = '-D_DEPLOYR_DISTRIBUTED_ENGINE_CLOUDR_MPI'
  
  CloudRHiCRBackends = CloudRProject.get_variable('CloudRHiCRBackends')
  CloudRHiCRFrontends = CloudRProject.get_variable('CloudRHiCRFrontends')

  additionalHiCRBackends = [ ] + CloudRHiCRBackends
  additionalHiCRFrontends = [ ] + CloudRHiCRFrontends
endif

##### Adding HiCR Dependency

# Getting full list of backends and frontends
HiCRBackends = [] + additionalHiCRBackends

if 'hwloc' not in additionalHiCRBackends
  HiCRBackends += 'hwloc'
endif

if 'pthreads' not in additionalHiCRBackends
  HiCRBackends += 'pthreads'
endif

HiCRFrontends = [ ] + additionalHiCRFrontends

# Getting HiCR dependency
HiCRProject = subproject('HiCR', required: true, default_options: [ 'backends=' + ','.join(HiCRBackends), 'frontends=' + ','.join(HiCRFrontends) ])
HiCRBuildDep = HiCRProject.get_variable('hicrBuildDep')
deployrDependencies += HiCRBuildDep

####### Getting Hopcroft Karp dependency

HopcroftKarpProject = subproject('hopcroft_karp', required: true)
HopcroftKarpBuildDep = HopcroftKarpProject.get_variable('HopcroftKarpBuildDep')
deployrDependencies += HopcroftKarpBuildDep

####### Creating DeployR dependency

# Warning handling option
warningAsErrorFlags=[]
if get_option('compileWarningsAsErrors') == true
  warningAsErrorFlags=[ '-Werror' ]
endif

DeployRBuildCppArgs = [
DeployRDistributedCppFlag,
 '-Wfatal-errors',
  warningAsErrorFlags
 ]

DeployRBuildIncludes = include_directories([
   'include'
  ])

####### Collect the dependencies

DeployRBuildDep = declare_dependency(
    compile_args: DeployRBuildCppArgs,
    include_directories: DeployRBuildIncludes,
    dependencies: deployrDependencies
    )

####### Build test / example targets only if HiCR is being loaded as a subproject

if meson.is_subproject() == false

  # Build example targets
  if get_option('buildExamples')
  subdir('examples')
  endif

  # Build test targets
  if get_option('buildTests')
  subdir('tests')
  endif

endif
