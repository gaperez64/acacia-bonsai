subdir ('spot-bin')
subdir ('acacia-plus')

test_files = {
               'demo-lily':
                 ['demo-v1.ltl', 'demo-v4.ltl', 'demo-v7.ltl', 'demo-v10.ltl',
                  'demo-v13.ltl', 'demo-v16.ltl', 'demo-v19.ltl', 'demo-v22.ltl',
                  'demo-v2.ltl', 'demo-v5.ltl', 'demo-v8.ltl', 'demo-v11.ltl',
                  'demo-v14.ltl', 'demo-v17.ltl', 'demo-v20.ltl', 'demo-v23.ltl',
                  'demo-v3.ltl', 'demo-v6.ltl', 'demo-v9.ltl', 'demo-v12.ltl',
                  'demo-v15.ltl', 'demo-v18.ltl', 'demo-v21.ltl', 'demo-v24.ltl'],
               'ltl2dba':
                 ['ltl2dba_01.ltl', 'ltl2dba_02.ltl', 'ltl2dba_03.ltl',
                  'ltl2dba_04.ltl', 'ltl2dba_05.ltl', 'ltl2dba_06.ltl', 'ltl2dba_07.ltl',
                  'ltl2dba_08.ltl', 'ltl2dba_09.ltl', 'ltl2dba_10.ltl', 'ltl2dba_11.ltl',
                  'ltl2dba_12.ltl', 'ltl2dba_13.ltl', 'ltl2dba_14.ltl', 'ltl2dba_15.ltl',
                  'ltl2dba_16.ltl', 'ltl2dba_17.ltl', 'ltl2dba_18.ltl', 'ltl2dba_19.ltl',
                  'ltl2dba_20.ltl', 'ltl2dba_21.ltl', 'ltl2dba_22.ltl', 'ltl2dba_23.ltl',
                  'ltl2dba_24.ltl', 'ltl2dba_25.ltl', 'ltl2dba_26.ltl', 'ltl2dba_27.ltl'],
               'ltl2dpa':
                 ['ltl2dpa_01.ltl', 'ltl2dpa_02.ltl', 'ltl2dpa_03.ltl',
                  'ltl2dpa_04.ltl', 'ltl2dpa_05.ltl', 'ltl2dpa_06.ltl', 'ltl2dpa_07.ltl',
                  'ltl2dpa_08.ltl', 'ltl2dpa_09.ltl', 'ltl2dpa_10.ltl', 'ltl2dpa_11.ltl',
                  'ltl2dpa_12.ltl', 'ltl2dpa_13.ltl', 'ltl2dpa_14.ltl', 'ltl2dpa_15.ltl',
                  'ltl2dpa_16.ltl', 'ltl2dpa_17.ltl', 'ltl2dpa_18.ltl', 'ltl2dpa_19.ltl',
                  'ltl2dpa_20.ltl', 'ltl2dpa_21.ltl', 'ltl2dpa_22.ltl', 'ltl2dpa_23.ltl',
                  'ltl2dpa_24.ltl']
             }


check_real_exe = find_program ('check-real-correct.sh')


foreach folder, testset : test_files
  foreach file : testset
    filename = 'ltl' / folder / file
    test (filename,
          check_real_exe,
          args : [ '-a', ab_exe,
                   '-r', ltlsynt_exe,
                   '-p', acacia_plus_py,
                   '-P', py.path (),
                   '-f', files (filename) ],
          env : py_env,
          timeout: 5)
  endforeach
endforeach
