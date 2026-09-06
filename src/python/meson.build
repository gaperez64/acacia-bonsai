# NOTE: this will use the Python version the "python" command corresponds with.
py = import('python').find_installation()

swig_cmd = find_program('swig')

# needed since we now include the Spot .i file.
spot_includedir = spot_dep.get_variable('includedir')
message(spot_includedir)

swig_gen = custom_target(
  'acacia_boomslang',
  input: 'acacia_boomslang.i',
  output: ['acacia_boomslang_wrap.cxx', 'acacia_boomslang.py'],
  command: [swig_cmd, '-c++', '-python', '-I' + spot_includedir, '-o', '@OUTPUT0@', '@INPUT@'],
  depend_files: files('python_interface.cc'),

  # Only the .py wrapper goes to site-packages; the .cxx feeds into the
  # extension_module build below and would just bloat the wheel otherwise.
  install: true,
  install_tag: [false, 'python-runtime'],
  install_dir: [false, py.get_install_dir()]
)

# /src plus the project root, whose build-tree half holds the generated
# acacia_build_config.hh.  Without it the extension module compiles against
# whatever src/configuration.hh happens to say rather than against the
# options this build was configured with.  See issue #130.
project_headers = include_directories('..', '../..')

# Build the extension module
module = py.extension_module('_acacia_boomslang',
  sources: [swig_gen, 'python_interface.cc'],
  dependencies: [spot_dep, posets_dep],
  include_directories: project_headers,
  install: true,
  install_tag: 'python-runtime',
)
