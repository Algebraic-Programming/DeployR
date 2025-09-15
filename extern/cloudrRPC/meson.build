project('CloudR', [ 'cpp'],
  subproject_dir : 'extern',
  default_options : [
  'cpp_std=c++20',
  'buildtype=release'
  ]
)

CloudRDependencies = [ ]

####### Getting HiCR dependency (if this is a standalone project)

CloudRHiCRBackends = [ 'mpi', 'hwloc', 'pthreads' ]
CloudRHiCRFrontends = [ 'RPCEngine', 'channel' ]

if meson.is_subproject() == false

# Selecting default HiCR Backends
HiCRProject = subproject('HiCR', required: true, default_options: [ 'backends=' + ','.join(CloudRHiCRBackends), 'frontends=' + ','.join(CloudRHiCRFrontends) ])
HiCRBuildDep = HiCRProject.get_variable('hicrBuildDep')
mpirunExecutable = HiCRProject.get_variable('mpirunExecutable')
CloudRDependencies += HiCRBuildDep

endif

####### Creating CloudR dependency

# Warning handling option
warningAsErrorFlags=[]
if get_option('compileWarningsAsErrors') == true
  warningAsErrorFlags=[ '-Werror' ]
endif

CloudRBuildCppArgs = [
 '-Wfatal-errors',
  warningAsErrorFlags
 ]

CloudRBuildIncludes = include_directories([
   'include'
  ])

####### Collect the dependencies

CloudRBuildDep = declare_dependency(
    compile_args: CloudRBuildCppArgs,
    include_directories: CloudRBuildIncludes,
    dependencies: CloudRDependencies
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
