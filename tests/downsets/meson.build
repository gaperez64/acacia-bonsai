tests_exe = executable ('tests', 'tests.cc',
                         include_directories : inc,
                         link_with : common_lib,
                         dependencies : [boost_dep, spot_dep, bddx_dep, gnulib_dep, stdsimd_dep],
                         c_args : '-O0' )  # Deactivate optimization, otherwise it takes ages.

simdbm_exe = executable ('simd-bm', 'simd-bm.cc',
                         include_directories : inc,
                         link_with : common_lib,
                         dependencies : [boost_dep, spot_dep, bddx_dep, gnulib_dep, stdsimd_dep] )
                         

test('antichains/vectors implementations', tests_exe, args : ['all', 'all'], suite : 'antichains')
