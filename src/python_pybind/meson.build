# NOTE: this will use the Python version the "python" command corresponds with.
py = import('python').find_installation()

pybind11_dep = dependency('pybind11')



# needed since we now include the Spot .i file.
#spot_includedir = spot_dep.get_variable('includedir')
#message(spot_includedir)

#swig_gen = custom_target(
#  'acacia_python',
#  input: 'acacia_python.i',
#  output: ['acacia_python_wrap.cxx', 'acacia_python.py'],
#  command: [swig_cmd, '-c++', '-python', '-I' + spot_includedir, '-o', '@OUTPUT0@', '@INPUT@'],
#  depend_files: files('python_interface.cc'),
#
#  # Install generated files automatically into site-packages
#  install: true,
#  install_dir: py.get_install_dir()
#)

project_headers = include_directories('..')  # everything in /src/

# Build the extension module
module = py.extension_module('acacia_bonsai_pybind',
    'pybind_interface.cc',
    dependencies : [pybind11_dep, spot_dep, posets_dep],
    include_directories: project_headers,
    install: true,
)
