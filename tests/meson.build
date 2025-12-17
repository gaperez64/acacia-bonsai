# meson test --suite check_real_all

message('Setting up realizability tests.')
python = find_program('python3')
expect_exit = find_program('expect-exit.sh')

get_real_args_cmd = run_command(
  python,
  files('generate_test_args.py'),
  '--directory',
  'ltl/realizable/',
  check: true  # if the Python script has a non-zero exit code, this will cause meson to error.
)

get_unreal_args_cmd = run_command(
  python,
  files('generate_test_args.py'),
  '--directory',
  'ltl/unrealizable/',
  check: true  # if the Python script has a non-zero exit code, this will cause meson to error.
)

tests = { 'real': get_real_args_cmd.stdout().strip().split('\n'),
          'unknown': get_unreal_args_cmd.stdout().strip().split('\n') }
expected = { 'real': '0', 'unknown': '1' }

message('Registering tests.')
foreach exp, test_list : tests
  foreach values : test_list
    values = values.split(';')
    input_val = values[0]
    output_val = values[1]
    ltl_val = values[2]
    test_name = values[3]
    test(
      test_name,
      expect_exit,
      args : [
        expected[exp],
        ab_exe,
        '-i', input_val,
        '-o', output_val,
        '-f', ltl_val,
        '-M', '10',
        '-K', '20',
        '-I', '1'
      ],
      suite : 'check_' + exp + '_all',
      timeout: 4
    )
  endforeach
endforeach
