testSuite = [ 'examples', 'deploy' ]

deploy = executable('deploy', [ 'deploy.cpp'], dependencies: [ DeployRBuildDep ])

if get_option('buildTests')
	test('deploy', deploy, args : [ ], suite: testSuite, workdir: deploy.path() + '.p' )
endif

