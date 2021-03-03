kdtree_sources = ['kdtree_tests.cc']



kdtree_exe = executable ('kdtree', kdtree_sources,
                         include_directories : inc,
                         link_with : common_lib)

test('vec_antichains', kdtree_exe, args : ['standard'], suite : 'antichains')
test('simd_antichains', kdtree_exe, args : ['simd'], suite : 'antichains')
