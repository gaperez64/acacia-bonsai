tests_sources = ['tests.cc']

tests_exe = executable ('tests', tests_sources,
                         include_directories : inc,
                         link_with : common_lib,
                         c_args : '-O0' )  # Deactivate optimization, otherwise it takes ages.

test('antichains/vectors implementations', tests_exe, args : ['all', 'all'], suite : 'antichains')
