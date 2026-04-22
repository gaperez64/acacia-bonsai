ab_sources = ['acacia-bonsai.cc']

if get_option('build_python')
  subdir ('python')
endif
#subdir ('python_pybind')

ab_exe = executable ('acacia-bonsai', ab_sources,
                     include_directories : inc,
                     install: true,
                     install_tag: 'cli',
                     dependencies : [posets_dep, spot_dep, bddx_dep, stdsimd_dep])

