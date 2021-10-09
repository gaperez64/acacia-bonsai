common_sources = [
  'common_aoutput.cc',
  'common_cout.cc',
  'common_file.cc',
  'common_finput.cc',
  'common_output.cc',
  'common_post.cc',
  'common_r.cc',
  'common_setup.cc',
]

common_lib = static_library ('common', common_sources,
                             include_directories : inc,
                             dependencies : [gnulib_dep, spot_dep, bddx_dep],
                             cpp_args : ['-DHAVE_CONFIG_H',
                                         '-DPACKAGE_NAME="@0@"'.format (meson.project_name()),
                                         '-DPACKAGE_VERSION="@0@"'.format (meson.project_version()),
                                         '-DPACKAGE_BUGREPORT="@0@"'.format (meson.project_name())
                                        ])

