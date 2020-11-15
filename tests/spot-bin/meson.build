
ltl2tgba_sources = ['ltl2tgba.cc']

ltlsynt_sources = ['ltlsynt.cc']

ltlsynt_exe = executable ('ltlsynt', ltlsynt_sources,
                          include_directories : inc,
                          link_with : [gnulib_lib, common_lib],
                          dependencies : spot_dep)

ltl2tgba_exe = executable ('ltl2tgba', ltl2tgba_sources,
                           include_directories : inc,
                           link_with : [gnulib_lib, common_lib],
                           dependencies : spot_dep)
