testSuite = [ 'examples', 'deploy' ]

if engine == 'mpi'
    exec = executable('mpi', [ 'mpi.cpp'], dependencies: DeployRBuildDep)
	if get_option('buildTests')
	  test('mpi', mpirunExecutable, args : [ '-np', '3', '--oversubscribe', exec.full_path(), meson.current_source_dir() + '/request.json'], timeout: 60, suite: testSuite )
	endif
endif

if engine == 'cloudr'
	exec = executable('cloudr', [ 'cloudr.cpp' ], dependencies: DeployRBuildDep)
	if get_option('buildTests')
	  test('cloudr-mpi', mpirunExecutable, args : [ '-np', '3', '--oversubscribe', exec.full_path(), meson.current_source_dir() + '/request.json'], timeout: 60, suite: testSuite )
	endif
endif