# meson test --suite check_real_all

message('Setting up realisability tests.')
python = find_program('python3')

get_real_args_cmd = run_command(
    python,
    files('generate_test_args.py'),
    '--directory',
    'ltl/realizable/',
    check: true  # if the Python script has a non-zero exit code, this will cause meson to error.
)

real_args = get_real_args_cmd.stdout().strip().split('\n')

message('Registering realizability tests.')
foreach real_arg_values: real_args
#    message('\tArgs: "', real_arg_values, '"')

    real_arg_values = real_arg_values.split(';')
    input_val = real_arg_values[0]
    output_val = real_arg_values[1]
    ltl_val = real_arg_values[2]
    test_name = real_arg_values[3]

    test(
        test_name,
        ab_exe,
        args : [
            '-i', input_val,
            '-o', output_val,
            '-f', ltl_val,
            '-M', '10',
            '-K', '20',
            '-I', '1'
        ],
        suite : 'check_real_all',
        timeout: 4
    )
endforeach

message('Setting up unrealisability tests.')
get_unreal_args_cmd = run_command(
    python,
    files('generate_test_args.py'),
    '--directory',
    'ltl/unrealizable/',
    check: true  # if the Python script has a non-zero exit code, this will cause meson to error.
)

unreal_args = get_unreal_args_cmd.stdout().strip().split('\n')

message('Registering realizability tests.')
foreach unreal_arg_values: unreal_args
#    message('\tArgs: "', real_arg_values, '"')

    unreal_arg_values = unreal_arg_values.split(';')
    input_val = unreal_arg_values[0]
    output_val = unreal_arg_values[1]
    ltl_val = unreal_arg_values[2]
    test_name = unreal_arg_values[3]

    test(
        test_name,
        ab_exe,
        args : [
            '-i', input_val,
            '-o', output_val,
            '-f', ltl_val,
            '-M', '10',
            '-K', '20',
            '-I', '1',
            '-!'
        ],
        suite : 'check_unreal_all',
        timeout: 4,
#        should_fail: true
    )
endforeach