"""Common DAOS library for setting up the compiler"""

from SCons.Script import GetOption, Exit
from SCons.Script import Configure


DESIRED_FLAGS = ['-fstack-usage',
                 '-Wno-sign-compare',
                 '-Wno-unused-parameter',
                 '-Wno-missing-field-initializers',
                 '-Wno-implicit-fallthrough',
                 '-Wno-ignored-attributes',
                 '-Wno-gnu-zero-variadic-macro-arguments',
                 '-Wno-tautological-constant-out-of-range-compare',
                 '-Wno-unused-command-line-argument',
                 '-Wmismatched-dealloc',
                 '-Wfree-nonheap-object',
                 '-Wframe-larger-than=4096']

# Compiler flags to prevent optimizing out security checks
DESIRED_FLAGS.extend(['-fno-strict-overflow', '-fno-delete-null-pointer-checks', '-fwrapv'])

# Compiler flags for stack hardening
DESIRED_FLAGS.extend(['-fstack-protector-strong', '-fstack-clash-protection'])

PP_ONLY_FLAGS = ['-Wno-parentheses-equality', '-Wno-builtin-requires-header',
                 '-Wno-unused-function']


def _base_setup(env):
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
    env.Append(CCFLAGS=['-g', '-Wextra', '-Wshadow', '-Wall', '-fpic'])

    env.AppendIfSupported(CCFLAGS=DESIRED_FLAGS)

    if '-Wmismatched-dealloc' in env['CCFLAGS']:
        env.AppendUnique(CPPDEFINES={'HAVE_DEALLOC': '1'})

    if build_type == 'debug':
        if compiler == 'gcc':
            env.AppendUnique(CCFLAGS=['-Og'])
        else:
            env.AppendUnique(CCFLAGS=['-O0'])
    else:
        if build_type == 'release':
            env.AppendUnique(CPPDEFINES='DAOS_BUILD_RELEASE')

        env.AppendUnique(CCFLAGS=['-O2'])
        _set_fortify_level(env)

    if build_type != 'release':
        env.AppendUnique(CPPDEFINES={'FAULT_INJECTION': '1'})
        env.AppendUnique(CPPDEFINES={'BUILD_PIPELINE': '1'})

    env.AppendUnique(CPPDEFINES={'CMOCKA_FILTER_SUPPORTED': '0'})

    env.AppendUnique(CPPDEFINES='_GNU_SOURCE')

    if compiler == 'icx' and not GetOption('no_rpath'):
        # Hack to add rpaths
        for path in env['ENV']['LD_LIBRARY_PATH'].split(':'):
            if 'oneapi' in path:
                env.AppendUnique(RPATH_FULL=[path])

    if GetOption('preprocess'):
        # Could refine this but for now, just assume these warnings are ok
        env.AppendIfSupported(CCFLAGS=PP_ONLY_FLAGS)

    env['BSETUP'] = compiler


def _check_flag_helper(context, compiler, ext, flag):
    """Helper function to allow checking for compiler flags"""
    if compiler in ["icc", "icpc"]:
        flags = ["-diag-error=10006", "-diag-error=10148", "-Werror-all", flag]
        # bug in older scons, need CFLAGS to exist, -O2 is default.
        context.env.Replace(CFLAGS=['-O2'])
    elif compiler in ["gcc", "g++"]:
        # pylint: disable=wrong-spelling-in-comment
        # remove -no- for test
        # There is a issue here when mpicc is a wrapper around gcc, in that we can pass -Wno-
        # options to the compiler even if it doesn't understand them but.  This would be tricky
        # to fix gcc only complains about unknown -Wno- warning options if the compile Fails
        # for other reasons anyway.
        test_flag = flag.replace("-Wno-", "-W")
        flags = ["-Werror", test_flag]
    else:
        flags = ["-Werror", flag]
    flags.append('-O1')
    context.Message(f'Checking {compiler} {flag} ')
    context.env.Replace(CCFLAGS=flags)
    ret = context.TryCompile("""
# include <features.h>
int main() {
    return 0;
}
""", ext)
    context.Result(ret)
    return ret


def _check_flag(context, flag):
    """Check C specific compiler flags"""
    return _check_flag_helper(context, context.env.get("CC"), ".c", flag)


def _check_flag_cc(context, flag):
    """Check C++ specific compiler flags"""
    return _check_flag_helper(context, context.env.get("CXX"), ".cpp", flag)


def _check_flags(env, config, key, value):
    """Check and append all supported flags"""
    if GetOption('help') or GetOption('clean'):
        return
    checked = []
    cxx = env.get('CXX')
    for flag in value:
        if flag in checked:
            continue
        insert = False
        if key == "CCFLAGS":
            if config.CheckFlag(flag) and (cxx is None or config.CheckFlagCC(flag)):
                insert = True
        elif key == "CFLAGS":
            if config.CheckFlag(flag):
                insert = True
        elif cxx is not None and config.CheckFlagCC(flag):
            insert = True
        if insert:
            env.AppendUnique(**{key: [flag]})
        checked.append(flag)


def _append_if_supported(env, **kwargs):
    """Check and append flags for construction variables"""
    cenv = env.Clone()
    config = Configure(cenv, custom_tests={'CheckFlag': _check_flag,
                                           'CheckFlagCC': _check_flag_cc})
    for key, value in kwargs.items():
        if key not in ["CFLAGS", "CXXFLAGS", "CCFLAGS"]:
            env.AppendUnique(**{key: value})
            continue
        _check_flags(env, config, key, value)

    config.Finish()


def _set_fortify_level(env):
    """Check what level of _FORTIFY_SOURCE is supported"""
    cenv = env.Clone()
    config = Configure(cenv, custom_tests={'CheckFlag': _check_flag})

    level = 3
    while level >= 2:
        if config.CheckFlag(f'-D_FORTIFY_SOURCE={level}'):
            env.AppendUnique(CPPDEFINES={'_FORTIFY_SOURCE': level})
            config.Finish()
            return
        level -= 1
    print('Could not determine level of FORTIFY_SOURCE to use')
    Exit(1)


def generate(env):
    """Add daos specific method to environment"""
    env.AddMethod(_base_setup, 'compiler_setup')
    env.AddMethod(_append_if_supported, "AppendIfSupported")


def exists(_env):
    """Tell SCons we exist"""
    return True
