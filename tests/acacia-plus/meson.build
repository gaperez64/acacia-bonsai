subdir ('lib')

py = import('python').find_installation('python2', modules : ['pygraph'])
py_env =  [ 'ACACIA_PLUS_LIB=' + acplus_lib.full_path (),
            'LTL2TGBA_BIN=' + ltl2tgba_exe.full_path () ]

acacia_plus_py = files ('acacia_plus.py')
