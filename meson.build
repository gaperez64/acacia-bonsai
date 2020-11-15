project ('Acacia Bonsai', ['c', 'cpp'],
         default_options : ['cpp_std=c++17'],
         version : '0.0.1',
         licence : 'GPL3')

spot_dep = dependency ('libspot')

inc = include_directories ('gnulib', 'lib')

subdir ('gnulib')
subdir ('lib')
subdir ('src')
subdir ('tests')
