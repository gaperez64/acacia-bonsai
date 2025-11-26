# meson test --suite check_real_all

message('Setting up realisability tests.')
python = find_program('python3')

get_args_cmd = run_command(
    python,
    files('generate_test_args.py'),
    '--directory',
    'ltl/realizable/'
)

# TODO: get errors as well.
args = get_args_cmd.stdout().strip().split('\n')
message('\tGot arguments', args)


tester = ab_exe

foreach arg_values: args
    message('Executing realizability tests.')
    message('\tArgs: "', arg_values, '"')

    test(
        'real_test',
        tester,
        args : [
            arg_values
        ],
        suite : 'check_real_all'
    )
endforeach
