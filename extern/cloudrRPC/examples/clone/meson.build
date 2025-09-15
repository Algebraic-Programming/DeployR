testSuite = [ 'examples', 'clone' ]

clone = executable('clone', [ 'clone.cpp'], dependencies: CloudRBuildDep)
if get_option('buildTests')
	test('clone', mpirunExecutable, args : [ '-np', '5', '--oversubscribe', clone.full_path() ], timeout: 60, suite: testSuite )
endif
