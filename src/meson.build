


ab_sources = ['ltlsynt.cc', 'common_finput.cc', 'common_setup.cc', 'common_aoutput.cc', 'common_file.cc',
             'common_cout.cc', 'common_post.cc', 'common_r.cc']

ab_exe = executable ('acacia-bonsai', ab_sources,
                     include_directories : inc,
                     cpp_args : ['-DHAVE_CONFIG_H',
                                 '-DPACKAGE_NAME="@0@"'.format (meson.project_name()),
                                 '-DPACKAGE_VERSION="@0@"'.format (meson.project_version()),
                                 '-DPACKAGE_BUGREPORT="@0@"'.format (meson.project_name())
                                ],
                     link_with : gnulib_lib,
                     dependencies : spot_dep)

