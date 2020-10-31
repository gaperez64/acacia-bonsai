project ('Acacia Bonsai', ['c', 'cpp'],
         version : '0.0.1',
         licence : 'GPL3')

add_global_arguments ('-DTEST', language: 'cpp')
spot_dep = dependency ('libspot')

inc = include_directories ('gnulib')

subdir ('gnulib')
subdir ('src')
