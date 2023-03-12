tester = executable('tester', sources : ['tester.cpp'])
test('full_arbiter-2', tester, args: ['tlsf/full_arbiter-2.tlsf'], timeout: 60, is_parallel: false)
test('full_arbiter-4', tester, args: ['tlsf/full_arbiter-4.tlsf'], timeout: 60, is_parallel: false)
test('round_robin_arbiter-3', tester, args: ['tlsf/round_robin_arbiter-3.tlsf'], timeout: 60, is_parallel: false)
test('robot_grid-2x2', tester, args: ['tlsf/robot_grid-2x2.tlsf'], timeout: 60, is_parallel: false)
test('load_balancer-1', tester, args: ['tlsf/load_balancer-1.tlsf'], timeout: 60, is_parallel: false)

# meson test full_arbiter-2 full_arbiter-4 round_robin_arbiter-3 robot_grid-2x2 load_balancer-1
