testSuite = [ 'examples', 'deploy' ]

if engine == 'mpi'
	mpi = executable('deploy', [ 'deploy.cpp'], dependencies: DeployRBuildDep)
	if get_option('buildTests')
	  test('mpi', mpirunExecutable, args : [ '-np', '3', '--oversubscribe', mpi.full_path(), meson.current_source_dir() + '/request.json'], timeout: 60, suite: testSuite )
	endif
endif