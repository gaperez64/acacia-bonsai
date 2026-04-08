ab_sources = ['acacia-bonsai.cc']

#subdir ('python')
subdir ('python_pybind')

ab_exe = executable ('acacia-bonsai', ab_sources,
                     include_directories : inc,
                     install: true,
                     dependencies : [posets_dep, spot_dep, bddx_dep, stdsimd_dep])

