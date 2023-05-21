ab_sources = ['acacia-bonsai.cc']

ab_exe = executable ('acacia-bonsai', ab_sources,
                     include_directories : inc,
                     link_with : [common_lib],
                     dependencies : [boost_dep, spot_dep, bddx_dep, gnulib_dep, stdsimd_dep])
