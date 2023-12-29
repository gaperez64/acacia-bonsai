tests_exe = executable ('tests', 'tests.cc',
                        include_directories : inc,
                        link_with : common_lib,
                        dependencies : [boost_dep, spot_dep, bddx_dep, gnulib_dep, stdsimd_dep],
                        c_args : '-O0' )  # Deactivate optimization, otherwise it takes ages.

kdtests_exe = executable ('kdtests', 'kdtree_backed.cc',
                          include_directories : inc,
                          link_with : common_lib,
                          dependencies : [boost_dep, spot_dep, bddx_dep, gnulib_dep] )

simdbm_exe = executable ('simd-bm', 'simd-bm.cc',
                         include_directories : inc,
                         link_with : common_lib,
                         dependencies : [boost_dep, spot_dep, bddx_dep, gnulib_dep, stdsimd_dep] )

downsetbm_exe = executable ('downset-bm', 'downset-bm.cc',
                            include_directories : inc,
                            link_with : common_lib,
                            dependencies : [boost_dep, spot_dep, bddx_dep, gnulib_dep, stdsimd_dep],
                            c_args : ['-O0', '-g'])

test('antichains/vectors random', downsetbm_exe,
     args : ['-p', 'build-query=0,intersection=52,union=0', 'vec,kd',
             'all', '-vv'], suite : 'antichains')

test('antichains/vectors implementations', tests_exe, args : ['all', 'all'], suite : 'antichains')

test('antichains/vectors kdtree-all', tests_exe, args : ['kdtree_backed', 'all'], suite : 'kdtree')
test('antichains/vectors kdtree-vec', tests_exe, args : ['kdtree_backed', 'vector_backed<char>'], suite : 'kdtree-nosimd')
test('antichains/vectors kdtree-arr', tests_exe, args : ['kdtree_backed', 'array_backed_<char, 2ul>'], suite : 'kdtree-nosimd')
test('antichains/vectors kdtree-arr-sum', tests_exe, args : ['kdtree_backed', 'array_backed_sum_<char, 2ul>'], suite : 'kdtree-nosimd')
test('antichains/vectors kdtests', kdtests_exe, suite : 'kdtree-nosimd')

test('antichains/vectors vec-or-tree-all', tests_exe, args : ['vector_or_kdtree_backed', 'all'], suite : 'vector-or-kdtree')
test('antichains/vectors vec-or-tree-vec', tests_exe, args : ['vector_or_kdtree_backed', 'vector_backed<char>'], suite : 'vector-or-kdtree-nosimd')
test('antichains/vectors vec-or-tree-arr', tests_exe, args : ['vector_or_kdtree_backed', 'array_backed_<char, 2ul>'], suite : 'vector-or-kdtree-nosimd')
test('antichains/vectors vec-or-tree-arr-sum', tests_exe, args : ['vector_or_kdtree_backed', 'array_backed_sum_<char, 2ul>'], suite : 'vector-or-kdtree-nosimd')
