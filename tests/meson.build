subdir ('spot-bin')
subdir ('acacia-plus')

tests_lily_ltl = ['demo-v1.ltl', 'demo-v4.ltl', 'demo-v7.ltl', 'demo-v10.ltl',
                  'demo-v13.ltl', 'demo-v16.ltl', 'demo-v19.ltl', 'demo-v22.ltl',
                  'demo-v2.ltl', 'demo-v5.ltl', 'demo-v8.ltl', 'demo-v11.ltl',
                  'demo-v14.ltl', 'demo-v17.ltl', 'demo-v20.ltl', 'demo-v23.ltl',
                  'demo-v3.ltl', 'demo-v6.ltl', 'demo-v9.ltl', 'demo-v12.ltl',
                  'demo-v15.ltl', 'demo-v18.ltl', 'demo-v21.ltl', 'demo-v24.ltl']

check_real_exe = find_program ('check-real-correct.sh')

foreach t : tests_lily_ltl
  test ('Test', check_real_exe, args : [ '-a', ab_exe,
                                         '-r', ltlsynt_exe,
                                         '-p', acacia_plus_py,
                                         '-P', py.path (),
                                         '-f', files ('demo-lily/' + t) ],
        env : py_env,
        timeout: 5)
endforeach
