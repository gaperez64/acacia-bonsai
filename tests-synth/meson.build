# meson test --suite synth

tester = find_program('process', meson.current_source_dir() / 'process')

test('full_arbiter-3', tester, args: [meson.current_source_dir() / 'tlsf/full_arbiter-3.tlsf'], is_parallel: false, suite: 'synth')
test('round_robin_arbiter-3', tester, args: [meson.current_source_dir() / 'tlsf/round_robin_arbiter-3.tlsf'], is_parallel: false, suite: 'synth')
test('robot_grid-2x2', tester, args: [meson.current_source_dir() / 'tlsf/robot_grid-2x2.tlsf'], is_parallel: false, suite: 'synth')
test('load_balancer-3', tester, args: [meson.current_source_dir() / 'tlsf/load_balancer-3.tlsf'], is_parallel: false, suite: 'synth')
test('shift-10', tester, args: [meson.current_source_dir() / 'tlsf/shift-10.tlsf'], is_parallel: false, suite: 'synth')
test('collector_v1-5', tester, args: [meson.current_source_dir() / 'tlsf/collector_v1-5.tlsf'], is_parallel: false, suite: 'synth')
test('detector-5', tester, args: [meson.current_source_dir() / 'tlsf/detector-5.tlsf'], is_parallel: false, suite: 'synth')
test('generalized_buffer-2', tester, args: [meson.current_source_dir() / 'tlsf/generalized_buffer-2.tlsf'], timeout: 60, is_parallel: false, suite: 'synth')
test('mux-10', tester, args: [meson.current_source_dir() / 'tlsf/mux-10.tlsf'], is_parallel: false, suite: 'synth')
test('narylatch-6', tester, args: [meson.current_source_dir() / 'tlsf/narylatch-6.tlsf'], is_parallel: false, suite: 'synth')
test('prioritized_arbiter-3', tester, args: [meson.current_source_dir() / 'tlsf/prioritized_arbiter-3.tlsf'], is_parallel: false, suite: 'synth')
# these take too long, but are interesting cases
#test('ltl2dba_U11', tester, args: [meson.current_source_dir() / 'tlsf/ltl2dba_U16.tlsf'], is_parallel: false, suite: 'synth')
#test('ltl2dba_U16', tester, args: [meson.current_source_dir() / 'tlsf/ltl2dba_U16.tlsf'], is_parallel: false, suite: 'synth')
#test('ltl2dba_U17', tester, args: [meson.current_source_dir() / 'tlsf/ltl2dba_U17.tlsf'], is_parallel: false, suite: 'synth')
#test('ltl2dba_U18', tester, args: [meson.current_source_dir() / 'tlsf/ltl2dba_U18.tlsf'], is_parallel: false, suite: 'synth')
