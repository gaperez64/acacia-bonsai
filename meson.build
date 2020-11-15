project ('Acacia Bonsai', ['c', 'cpp'],
         version : '0.0.1',
         licence : 'GPL3')

add_global_arguments ('-std=c++20', language: 'cpp')
spot_dep = dependency ('libspot')

inc = include_directories ('gnulib', 'lib')

subdir ('gnulib')
subdir ('lib')
subdir ('src')
subdir ('tests')
