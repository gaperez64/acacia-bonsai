project('spot', 'cpp',
  meson_version : '>=0.57.0',
)

mod = import('unstable_external_project')

run_command ('autoreconf', '-fvi')

p = mod.add_project('configure',
  configure_options : ['--disable-devel',
                       '--disable-debug',
                       '--enable-optimizations',
                       '--disable-python',
                       '--disable-static'
                      ],
)

bddx_dep = p.dependency ('bddx')
spot_dep = p.dependency ('spot')
