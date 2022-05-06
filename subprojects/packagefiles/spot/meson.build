project('spot', 'cpp',
  meson_version : '>=0.57.0',
)

mod = import('unstable_external_project')
fs = import ('fs')
if not fs.exists ('configure')
  run_command ('autoreconf', '-fvi')
endif

p = mod.add_project('configure',
  configure_options : ['--disable-devel',
                       '--disable-debug',
                       '--enable-optimizations',
                       '--disable-python',
                       '--disable-static',
                       '--disable-python',
                       '--disable-doxygen'
                      ],
)

bddx_dep = p.dependency ('bddx')
spot_dep = p.dependency ('spot')
