# meson test --suite synth

tester = executable('tester', sources : ['tester-synth.cc'])

test('full_arbiter-3', tester, args: ['tlsf/full_arbiter-3.tlsf'], is_parallel: false, suite: 'synth')
test('round_robin_arbiter-3', tester, args: ['tlsf/round_robin_arbiter-3.tlsf'], is_parallel: false, suite: 'synth')
test('robot_grid-2x2', tester, args: ['tlsf/robot_grid-2x2.tlsf'], is_parallel: false, suite: 'synth')
test('load_balancer-3', tester, args: ['tlsf/load_balancer-3.tlsf'], is_parallel: false, suite: 'synth')
test('shift-10', tester, args: ['tlsf/shift-10.tlsf'], is_parallel: false, suite: 'synth')
test('collector_v1-5', tester, args: ['tlsf/collector_v1-5.tlsf'], is_parallel: false, suite: 'synth')
test('detector-5', tester, args: ['tlsf/detector-5.tlsf'], is_parallel: false, suite: 'synth')
test('generalized_buffer-2', tester, args: ['tlsf/generalized_buffer-2.tlsf'], is_parallel: false, suite: 'synth')
test('mux-10', tester, args: ['tlsf/mux-10.tlsf'], is_parallel: false, suite: 'synth')
test('narylatch-6', tester, args: ['tlsf/narylatch-6.tlsf'], is_parallel: false, suite: 'synth')
test('prioritized_arbiter-3', tester, args: ['tlsf/prioritized_arbiter-3.tlsf'], is_parallel: false, suite: 'synth')


# meson test --suite comp

tester_c = executable('tester_c', sources : ['tester-comp.cc'])

test('c-full_arbiter-3', tester_c, args: ['tlsf/full_arbiter-3.tlsf'], is_parallel: false, suite: 'comp')
test('c-round_robin_arbiter-3', tester_c, args: ['tlsf/round_robin_arbiter-3.tlsf'], is_parallel: false, suite: 'comp')
test('c-robot_grid-2x2', tester_c, args: ['tlsf/robot_grid-2x2.tlsf'], is_parallel: false, suite: 'comp')
test('c-load_balancer-3', tester_c, args: ['tlsf/load_balancer-3.tlsf'], is_parallel: false, suite: 'comp')
test('c-shift-10', tester_c, args: ['tlsf/shift-10.tlsf'], is_parallel: false, suite: 'comp')
test('c-collector_v1-5', tester_c, args: ['tlsf/collector_v1-5.tlsf'], is_parallel: false, suite: 'comp')
test('c-detector-5', tester_c, args: ['tlsf/detector-5.tlsf'], is_parallel: false, suite: 'comp')
test('c-generalized_buffer-2', tester_c, args: ['tlsf/generalized_buffer-2.tlsf'], is_parallel: false, suite: 'comp')
test('c-mux-10', tester_c, args: ['tlsf/mux-10.tlsf'], is_parallel: false, suite: 'comp')
test('c-narylatch-6', tester_c, args: ['tlsf/narylatch-6.tlsf'], is_parallel: false, suite: 'comp')
test('c-prioritized_arbiter-3', tester_c, args: ['tlsf/prioritized_arbiter-3.tlsf'], is_parallel: false, suite: 'comp')

# meson test --suite synth --suite comp