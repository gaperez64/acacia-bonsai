# meson test --suite synth

tester = find_program('process-synth', meson.current_source_dir() / 'process-synth.sh')

testset = ['01',
           'full_arbiter-3',
           'round_robin_arbiter-3',
           'robot_grid-2x2',
           'load_balancer_2',
           'load_balancer-3',
           'shift-10',
           'collector_v1-5',
           'detector_2',
           'detector_3',
           'detector-5',
           'generalized_buffer-2',
           'mux-10',
           'narylatch-6',
           'prioritized_arbiter-3',
           'ltl2dba_U11',
           # '07',           # has too many acc states
           # 'ltl2dba_U16',  # these take too long
           # 'ltl2dba_U17',
           # 'ltl2dba_U18'
           ]

foreach t : testset
  filename = meson.source_root() / 'tests/ltl/realizable' / t + '.tlsf'
  test(t, tester, args: [filename, 
                         meson.project_build_root()],
       is_parallel: false, suite: 'synth')
endforeach

# meson test --suite comp

tester_c = find_program('process-comp', meson.current_source_dir() / 'process-comp.sh')

foreach t : testset
  filename = meson.source_root() / 'tests/ltl/realizable' / t + '.tlsf'
  test('c-' + t, tester, args: [filename,
                                meson.project_build_root()],
       is_parallel: false, suite: 'comp')
endforeach
