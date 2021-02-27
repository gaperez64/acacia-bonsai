kdtree_sources = ['kdtree_tests.cc']



kdtree_exe = executable ('kdtree', kdtree_sources,
                         include_directories : inc,
                         link_with : common_lib)

test('test1', kdtree_exe, args : ['first', 'second'])
