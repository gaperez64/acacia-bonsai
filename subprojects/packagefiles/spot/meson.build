project('spot', 'cpp',
  meson_version : '>=0.57.0',
)

mod = import('unstable-external_project')
fs = import ('fs')
if not fs.exists ('configure')
  run_command ('autoreconf', '-fvi', check: true)
endif

p = mod.add_project('configure',
  env : [ 'CXXFLAGS=' ],
  configure_options : ['--disable-devel',
                       '--disable-debug',
                       '--enable-optimizations',
                       '--disable-python',
                       '--disable-static',
                       '--disable-python',
                       '--disable-doxygen',
                       '--enable-max-accsets=64'
                      ],
)

bddx_dep = p.dependency ('bddx')
spot_dep = p.dependency ('spot')
