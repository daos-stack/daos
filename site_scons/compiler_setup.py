"""Common DAOS library for setting up the compiler"""

from SCons.Script import GetOption, Exit, Configure

DESIRED_FLAGS = ['-Wno-gnu-designator',
                 '-Wno-missing-braces',
                 '-fstack-usage',
                 '-Wno-ignored-attributes',
                 '-Wno-gnu-zero-variadic-macro-arguments',
                 '-Wno-tautological-constant-out-of-range-compare',
                 '-Wno-unused-command-line-argument',
                 '-Wframe-larger-than=4096']

# Compiler flags to prevent optimizing out security checks
DESIRED_FLAGS.extend(['-fno-strict-overflow', '-fno-delete-null-pointer-checks',
                      '-fwrapv'])

# Compiler flags for stack hardening
DESIRED_FLAGS.extend(['-fstack-protector-strong', '-fstack-clash-protection'])

PP_ONLY_FLAGS = ['-Wno-parentheses-equality', '-Wno-builtin-requires-header',
                 '-Wno-unused-function']


def base_setup(env, prereqs=None):
    """Setup the scons environment for the compiler

    Include all our preferred compile options for the chosen
    compiler and build type.
    """

    if GetOption('help') or GetOption('clean'):
        return

    compiler = env['CC']

    build_type = env['BUILD_TYPE']
    print(f'Setting up compile environment for {compiler}')
    print(f"Build type is '{build_type}'")

    prev_compiler = env.get('BSETUP', False)
    if prev_compiler:
        if prev_compiler != compiler:
            print('Env is already setup for a different compiler')
        print('Env already setup')
        Exit(2)

    # Turn on -Wall first, then DESIRED_FLAGS may disable some of the options
    # that this brings in.
    env.Append(CCFLAGS=['-g',
                        '-Wshadow',
                        '-Wall',
                        '-fpic'])

    env.AppendIfSupported(CCFLAGS=DESIRED_FLAGS)

    if build_type == 'debug':
        if compiler == 'gcc':
            env.AppendUnique(CCFLAGS=['-Og'])
        else:
            env.AppendUnique(CCFLAGS=['-O0'])
    else:
        if build_type == 'release':
            env.AppendUnique(CPPDEFINES='DAOS_BUILD_RELEASE')

        env.AppendUnique(CCFLAGS=['-O2'])
        env.AppendUnique(CPPDEFINES={'_FORTIFY_SOURCE': '2'})

    if build_type != 'release':
        env.AppendUnique(CPPDEFINES={'FAULT_INJECTION': '1'})

    env.AppendUnique(CPPDEFINES={'CMOCKA_FILTER_SUPPORTED': '0'})

    env.AppendUnique(CPPDEFINES='_GNU_SOURCE')

    cenv = env.Clone()
    cenv.Append(CFLAGS='-Werror')
    config = Configure(cenv)
    if config.CheckHeader('stdatomic.h'):
        env.AppendUnique(CPPDEFINES={'HAVE_STDATOMIC': '1'})
    config.Finish()

    if compiler == 'icx' and not GetOption('no_rpath'):
        # Hack to add rpaths
        for path in env['ENV']['LD_LIBRARY_PATH'].split(':'):
            if 'oneapi' in path:
                env.AppendUnique(RPATH_FULL=[path])

    if GetOption('preprocess'):
        # Could refine this but for now, just assume these warnings are ok
        env.AppendIfSupported(CCFLAGS=PP_ONLY_FLAGS)

    env['BSETUP'] = compiler
