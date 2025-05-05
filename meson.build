project('DeployR', [ 'cpp', 'c'],
  subproject_dir : 'extern',
  default_options : [
  'cpp_std=c++20',
  'buildtype=release'
  ]
)

####### Storage for DeployR dependencies

deployrDependencies = [ ]

####### Getting HiCR dependency (only if not a subproject)

if meson.is_subproject() == false

  # Selecting default HiCR Backends
  HiCRBackends = [ 'hwloc', 'pthreads', 'boost' ]

  # Getting selected distributed engine

  distributedEngine = get_option('distributedEngine')

  # If, selected, adding the dependency for the distributed engine
  if distributedEngine != 'none'
    HiCRBackends += distributedEngine
  endif

  HiCRProject = subproject('HiCR', required: true, default_options: [ 'backends=' + ','.join(HiCRBackends), 'frontends=tasking' ])
  HiCRBuildDep = HiCRProject.get_variable('hicrBuildDep')
  deployrDependencies += HiCRBuildDep

endif

####### Creating DeployR dependency

subdir('include/deployr')

# Warning handling option
warningAsErrorFlags=[]
if get_option('compileWarningsAsErrors') == true
  warningAsErrorFlags=[ '-Werror' ]
endif

DeployRBuildCppArgs = [
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
