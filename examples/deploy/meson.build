testSuite = [ 'examples', 'deploy' ]

if 'mpi' in engines
    exec = executable('mpi', [ 'mpi.cpp'], dependencies: DeployRBuildDep)
	if get_option('buildTests')
	  test('mpi', mpirunExecutable, args : [ '-np', '3', '--oversubscribe', exec.full_path(), meson.current_source_dir() + '/request.json'], timeout: 60, suite: testSuite )
	endif
endif

if 'cloudr' in engines
	exec = executable('cloudr', [ 'cloudr.cpp' ], dependencies: DeployRBuildDep)
	if get_option('buildTests')
	  test('cloudr', mpirunExecutable, args : [ '-np', '3', '--oversubscribe', exec.full_path(), meson.current_source_dir() + '/request.json'], timeout: 60, suite: testSuite )
	endif
endif