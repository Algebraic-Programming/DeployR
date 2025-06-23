testSuite = [ 'examples', 'deploy' ]

exec = executable('deploy', [ 'deploy.cpp'], dependencies: DeployRBuildDep)

if engine == 'mpi'
	if get_option('buildTests')
	  test('mpi', mpirunExecutable, args : [ '-np', '3', '--oversubscribe', exec.full_path(), meson.current_source_dir() + '/request.json'], timeout: 60, suite: testSuite )
	endif
endif

if engine == 'cloudr-mpi'
	if get_option('buildTests')
	  test('cloudr-mpi', mpirunExecutable, args : [ '-np', '3', '--oversubscribe', exec.full_path(), meson.current_source_dir() + '/request.json'], timeout: 60, suite: testSuite )
	endif
endif