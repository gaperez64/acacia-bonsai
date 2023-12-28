tests_exe = executable ('tests', 'tests.cc',
                         include_directories : inc,
                         link_with : common_lib,
                         dependencies : [boost_dep, spot_dep, bddx_dep, gnulib_dep, stdsimd_dep],
                         c_args : '-O0' )  # Deactivate optimization, otherwise it takes ages.

simdbm_exe = executable ('simd-bm', 'simd-bm.cc',
                         include_directories : inc,
                         link_with : common_lib,
                         dependencies : [boost_dep, spot_dep, bddx_dep, gnulib_dep, stdsimd_dep] )

downsetbm_exe = executable ('downset-bm', 'downset-bm.cc',
                            include_directories : inc,
                            link_with : common_lib,
                            dependencies : [boost_dep, spot_dep, bddx_dep, gnulib_dep, stdsimd_dep] )

test('antichains/vectors implementations', tests_exe, args : ['all', 'all'], suite : 'antichains')

test('antichains/vectors kdtree-backed', tests_exe, args : ['kdtree_backed', 'all'], suite : 'kdtree')
test('antichains/vectors kdtree-backed', tests_exe, args : ['kdtree_backed', 'vector_backed<char>'], suite : 'kdtree-nosimd')
test('antichains/vectors kdtree-backed', tests_exe, args : ['kdtree_backed', 'array_backed_<char, 2ul>'], suite : 'kdtree-nosimd')
test('antichains/vectors kdtree-backed', tests_exe, args : ['kdtree_backed', 'array_backed_sum_<char, 2ul>'], suite : 'kdtree-nosimd')

test('antichains/vectors vekd-backed', tests_exe, args : ['vector_or_kdtree_backed', 'all'], suite : 'vector-or-kdtree')
test('antichains/vectors vekd-backed', tests_exe, args : ['vector_or_kdtree_backed', 'vector_backed<char>'], suite : 'vector-or-kdtree-nosimd')
test('antichains/vectors vekd-backed', tests_exe, args : ['vector_or_kdtree_backed', 'array_backed_<char, 2ul>'], suite : 'vector-or-kdtree-nosimd')
test('antichains/vectors vekd-backed', tests_exe, args : ['vector_or_kdtree_backed', 'array_backed_sum_<char, 2ul>'], suite : 'vector-or-kdtree-nosimd')
