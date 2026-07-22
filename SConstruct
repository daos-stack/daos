"""Build DAOS"""
import os
import sys

import SCons.Warnings
from prereq_tools import PreReqComponent  # pylint: disable=reimported

if sys.version_info.major < 3:
    print(""""Python 2.7 is no longer supported in the DAOS build.
Install python3 version of SCons.   On some platforms this package does not
install the scons binary so your command may need to use scons-3 instead of
scons or you will need to create an alias or script by the same name to
wrap scons-3.""")
    Exit(1)

SCons.Warnings.warningAsException()


def add_command_line_options():
    """Add command line options"""

    AddOption('--preprocess',
              dest='preprocess',
              action='store_true',
              default=False,
              help='Preprocess selected files for profiling')
    AddOption('--no-rpath',
              dest='no_rpath',
              action='store_true',
              default=False,
              help='Disable rpath')
    AddOption('--analyze-stack',
              dest='analyze_stack',
              metavar='ARGSTRING',
              default=None,
              help='Gather stack usage statistics after build')

    # We need to sometimes use alternate tools for building and need to add them to the PATH in the
    # environment.
    AddOption('--prepend-path',
              dest='prepend_path',
              default=None,
              help="String to prepend to PATH environment variable.")

    # Allow specifying the locale to be used.  Default "en_US.UTF8"
    AddOption('--locale-name',
              dest='locale_name',
              default='en_US.UTF8',
              help='locale to use for building. [%default]')

    AddOption('--require-optional',
              dest='require_optional',
              action='store_true',
              default=False,
              help='Fail the build if check_component fails')

    AddOption('--build-deps',
              dest='build_deps',
              type='choice',
              choices=['fetch', 'yes', 'no', 'only'],
              default='no',
              help="Automatically download and build sources.  (fetch|yes|no|only) [no]")

    AddOption('--skip-download',
              dest='skip_download',
              action='store_true',
              default=False,
              help="Assume the source for prerequisites is already downloaded")

    # We want to be able to check what dependencies are needed without
    # doing a build, similar to --dry-run.  We can not use --dry-run
    # on the command line because it disables running the tests for the
    # the dependencies.  So we need a new option
    AddOption('--check-only',
              dest='check_only',
              action='store_true',
              default=False,
              help="Check dependencies only, do not download or build.")

    # Need to be able to look for an alternate build.config file.
    AddOption('--build-config',
              dest='build_config',
              default=os.path.join(Dir('#').abspath, 'utils', 'build.config'),
              help='build config file to use. [%default]')

    AddOption('--deps-as-gitmodules-subdir',
              dest='deps_as_gitmodules_subdir',
              default=None,
              help='Ignore the versions/branches/patches specified in build.config and \
                    use the specified relative sub-directory containing all \
                    dependencies as git submodules instead')


def parse_and_save_conf(env, opts_file):
    """Parse daos.conf

    This only sets the initial values, most are set within prereqs as that's where they are used
    and the defaults are calculated."""

    opts = Variables(opts_file)

    opts.Add(EnumVariable('SCONS_ENV', "Default SCons environment inheritance",
                          'minimal', ['minimal', 'full'], ignorecase=2))

    opts.Add('GO_BIN', 'Full path to go binary', None)

    opts.Add(PathVariable('ENV_SCRIPT', "Location of environment script",
                          os.path.expanduser('~/.scons_localrc'),
                          PathVariable.PathAccept))

    # Finally parse the command line options and save to file if required.
    opts.Update(env)

    return opts


def build_misc(build_prefix):
    """Build miscellaneous items"""
    # install the configuration files
    common = os.path.join('utils', 'config')
    path = os.path.join(build_prefix, common)
    SConscript(os.path.join(common, 'SConscript'), variant_dir=path, duplicate=0)

    # install certificate generation files
    common = os.path.join('utils', 'certs')
    path = os.path.join(build_prefix, common)
    SConscript(os.path.join(common, 'SConscript'), variant_dir=path, duplicate=0)


def load_local(env_script, env):
    """Function for importing custom scons file.

    Making this a function allows us to export 'env' without namespace pollution in the parent.
    """
    # pylint: disable=unused-argument
    SConscript(env_script, exports=['env'])


# Environment variables that are kept when SCONS_ENV=minimal (the default).
MINIMAL_ENV = ('HOME', 'TERM', 'SSH_AUTH_SOCK', 'http_proxy', 'https_proxy', 'PKG_CONFIG_PATH',
               'MODULEPATH', 'MODULESHOME', 'MODULESLOADED', 'I_MPI_ROOT', 'COVFILE')

# Environment variables that are also kept when LD_PRELOAD is set.
PRELOAD_ENV = ('LD_PRELOAD', 'D_LOG_FILE', 'DAOS_AGENT_DRPC_DIR', 'D_LOG_MASK', 'DD_MASK',
               'DD_SUBSYS', 'D_IL_MAX_EQ', 'D_IL_ENFORCE_EXEC_ENV', 'D_IL_COMPATIBLE')


def scons():
    """Perform the build"""

    deps_env = Environment()
    # Ensure 'install-sandbox' option is defined early
    deps_env.Tool('install')

    # Silence deprecation warning so it doesn't fail the build
    SetOption('warn', ['no-python-version'])

    add_command_line_options()

    # Scons strips out the environment, however that is not always desirable so add back in
    # several options that might be needed.

    opts_file = os.path.join(Dir('#').abspath, 'daos.conf')

    opts = parse_and_save_conf(deps_env, opts_file)

    if deps_env.get('SCONS_ENV') == 'full':
        deps_env.Replace(ENV=os.environ.copy())
    else:

        def _copy_env(var_list):
            for var in var_list:
                value = os.environ.get(var)
                if value:
                    real_env[var] = value

        real_env = deps_env['ENV']

        _copy_env(MINIMAL_ENV)

        # This is used for the daos_build test, we could move to using SCONS_ENV=full instead to
        # avoid this logic however this still has known issues.
        if 'LD_PRELOAD' in os.environ:
            _copy_env(PRELOAD_ENV)

        deps_env.Replace(ENV=real_env)

        venv_path = os.environ.get('VIRTUAL_ENV')
        if venv_path:
            deps_env.PrependENVPath('PATH', os.path.join(venv_path, 'bin'))
            deps_env['ENV']['VIRTUAL_ENV'] = venv_path

    pre_path = GetOption('prepend_path')
    if pre_path:
        deps_env.PrependENVPath('PATH', pre_path)

    locale_name = GetOption('locale_name')
    if locale_name:
        deps_env['ENV']['LC_ALL'] = locale_name

    # parse a ~/.scons_localrc file if it exists.
    env_script = deps_env.get('ENV_SCRIPT')
    if os.path.exists(env_script):
        load_local(env_script, deps_env)

    # Perform this check early before loading PreReqs as if this header is missing then we want
    # to exit before building any dependencies.
    if not GetOption('help'):

        config = deps_env.Configure()
        if not config.CheckHeader('stdatomic.h'):
            Exit('stdatomic.h is required to compile DAOS, update your compiler or distro version')
        config.Finish()

    # Define and load the components.  This will add more values to opt.
    prereqs = PreReqComponent(deps_env, opts)

    gitmodules_subdir = GetOption("deps_as_gitmodules_subdir")
    if gitmodules_subdir:
        print(f"WARNING: --deps-as-gitmodules-subdir specified; ignoring build.config "
              f"versions, branches, and patches and using gitmodules installed in "
              f"{gitmodules_subdir} instead")
        gitmodules_absdir = os.path.join(Dir('#').abspath, gitmodules_subdir)
        if not os.path.isdir(gitmodules_absdir):
            print(f"ERROR: --deps-as-gitmodules-subdir was specified but {gitmodules_absdir} "
                  f"is not a valid directory.")
            Exit(-1)

    # Now save the daos.conf file before attempting to build anything.  This means that options
    # are sticky even if there's a failed build.
    opts.Save(opts_file, deps_env)

    # This will add a final 'DEPS' value to opts but it will not be persistent.
    prereqs.run_build(opts)

    if GetOption('build_deps') == 'only':
        prereqs.save_build_info()
        print('Exiting because --build-deps=only was set')
        Exit(0)

    env = deps_env.Clone(tools=['extra', 'textfile', 'daos_builder', 'compiler_setup'])

    if not GetOption('help'):
        if prereqs.check_component('valgrind_devel'):
            env.AppendUnique(CPPDEFINES=["D_HAS_VALGRIND"])

    conf_dir = ARGUMENTS.get('CONF_DIR', '$PREFIX/etc')

    env.Alias('install', '$PREFIX')

    base_env = env.Clone()

    if not GetOption('help') and not GetOption('clean'):
        base_env_mpi = env.d_configure_mpi()
        if not base_env_mpi:
            print("\nSkipping compilation for tests that need MPI")
            print("Install and load mpich or openmpi\n")
    else:
        base_env_mpi = None

    env.compiler_setup()
    build_prefix = prereqs.get_src_build_dir()
    comp_prefix = prereqs.get_build_dir()

    args = GetOption('analyze_stack')
    if args is not None:
        env.Tool('stack_analyzer', daos_prefix=build_prefix, comp_prefix=comp_prefix, args=args)

    Export('env', 'base_env', 'base_env_mpi', 'prereqs', 'conf_dir')

    # generate targets in specific build dir to avoid polluting the source code
    path = os.path.join(build_prefix, 'src')
    SConscript(os.path.join('src', 'SConscript'), variant_dir=path, duplicate=0)

    prereqs.save_build_info()
    # also install to $PREFIX/lib to work with existing avocado test code
    if prereqs.test_requested():
        env.Install('$PREFIX/lib/daos/TESTING/ftest/util', ['site_scons/env_modules.py'])
        env.Install('$PREFIX/lib/daos/TESTING/ftest/', ['ftest.sh', "requirements-ftest.txt"])

    env.Install("$PREFIX/lib64/daos", "VERSION")

    env.Install(os.path.join(conf_dir, 'bash_completion.d'), 'utils/completion/daos.bash')

    build_misc(build_prefix)

    Default(build_prefix)

    # an "rpms" target
    env.Command('rpms', '', 'make -C utils/rpms rpms')

    Help(opts.GenerateHelpText(env), append=True)


if __name__ == "SCons.Script":
    scons()
