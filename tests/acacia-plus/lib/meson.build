acplus_sources = [ 'antichain.c', 'backward_algorithm.c', 'bits.c', 'cache.c',
  'counting_function.c', 'forward_algorithm.c', 'hash_table.c', 'linked_list.c',
  'memory_management.c', 'safety_game.c', 'synthesis.c', 'tbucw.c',
  'transition_system.c', 'tuple.c', 'vector.c' ]

cc = meson.get_compiler('c')

acplus_deps = [ dependency('glib-2.0', version: '>=2.38'),
                cc.find_library('m', required : false) ]

acplus_lib = shared_library ('acacia_plus', acplus_sources,
                             dependencies: acplus_deps,
                             include_directories : inc)
