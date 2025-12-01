# meson test --suite check_real_all

message('Setting up realisability tests.')
python = find_program('python3')

get_args_cmd = run_command(
    python,
    files('generate_test_args.py'),
    '--directory',
    'ltl/realizable/'
)

# TODO: get test parser errors as well.
args = get_args_cmd.stdout().strip().split('\n')
message('\tGot arguments', args)

foreach arg_values: args
    message('Executing realizability tests.')
    message('\tArgs: "', arg_values, '"')

    arg_values = arg_values.split(';')
    input_val = arg_values[0]
    output_val = arg_values[1]
    ltl_val = arg_values[2]
    test_name = arg_values[3]

    test(
        test_name,
        ab_exe,
        args : [
            '-i', input_val,
            '-o', output_val,
            '-f', ltl_val,
            '-K', '25'
        ],
        suite : 'check_real_all',
        timeout: 2
    )
endforeach
