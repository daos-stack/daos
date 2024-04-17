"""Build DAOS"""

import errno
import os
import subprocess  # nosec
import sys
import time

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
              choices=['yes', 'no', 'only', 'build-only'],
              default='no',
              help="Automatically download and build sources.  (yes|no|only|build-only) [no]")

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


def parse_and_save_conf(env, opts_file):
    """Parse daos.conf

    This only sets the initial values, most are set within prereqs as that's where they are used
    and the defaults are calculated."""

    opts = Variables(opts_file)

    opts.Add(EnumVariable('SCONS_ENV', "Default SCons environment inheritance",
                          'minimal', ['minimal', 'full'], ignorecase=2))

    opts.Add(BoolVariable('STATIC_FUSE', "Build with static libfuse library", 1))

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


def update_rpm_version(version, tag):
    """ Update the version (and release) in the RPM spec file """

    # pylint: disable=consider-using-f-string
    spec = open("utils/rpms/daos.spec", "r").readlines()  # pylint: disable=consider-using-with
    current_version = 0
    release = 0
    for line_num, line in enumerate(spec):
        if line.startswith("Version:"):
            current_version = line[line.rfind(' ') + 1:].rstrip()
            if version < current_version:
                print("You cannot create a new version ({}) lower than the RPM "
                      "spec file has currently ({})".format(version,
                                                            current_version))
                return False
            if version > current_version:
                spec[line_num] = "Version:       {}\n".format(version)
        if line.startswith("Release:"):
            if version == current_version:
                current_release = int(line[line.rfind(' ') + 1:line.find('%')])
                release = current_release + 1
            else:
                release = 1
            spec[line_num] = "Release:       {}%{{?relval}}%{{?dist}}\n".\
                             format(release)
        if line == "%changelog\n":
            cmd = 'rpmdev-packager'
            try:
                # pylint: disable=consider-using-with
                pkg_st = subprocess.Popen(cmd, stdout=subprocess.PIPE)  # nosec
                packager = pkg_st.communicate()[0].strip().decode('UTF-8')
            except OSError:
                print("You need to have the rpmdev-packager tool (from the "
                      "rpmdevtools RPM on EL7) in order to make releases.\n\n"
                      "Additionally, you should define %packager in "
                      "~/.rpmmacros as such:\n"
                      "%packager	John A. Doe <john.doe@intel.com>"
                      "so that package changelog entries are well defined")
                return False
            date_str = time.strftime('%a %b %d %Y', time.gmtime())
            spec.insert(line_num + 1, "\n")
            spec.insert(line_num + 1,
                        "- Version bump up to {}\n".format(tag))
            spec.insert(line_num + 1,
                        '* {} {} - {}-{}\n'.format(date_str,
                                                   packager,
                                                   version,
                                                   release))
            break
    open("utils/rpms/daos.spec", "w").writelines(spec)  # pylint: disable=consider-using-with

    return True


def check_for_release_target():  # pylint: disable=too-many-locals
    """Update GitHub for release tag"""
    if COMMAND_LINE_TARGETS == ['release']:
        # pylint: disable=consider-using-f-string
        try:
            # pylint: disable=import-outside-toplevel
            import github
            import pygit2
            import yaml
        except ImportError:
            print("You need yaml, pygit2 and pygithub python modules to create releases")
            Exit(1)

        variables = Variables()

        variables.Add('RELEASE', 'Set to the release version to make', None)
        variables.Add('RELEASE_BASE', 'Set to the release version to make',
                      'master')
        variables.Add('ORG_NAME', 'The GitHub project to do the release on.',
                      'daos-stack')
        variables.Add('REMOTE_NAME', 'The remoten name release on.', 'origin')

        env = Environment(variables=variables)

        org_name = env['ORG_NAME']
        remote_name = env['REMOTE_NAME']
        base_branch = env['RELEASE_BASE']

        try:
            tag = env['RELEASE']
        except KeyError:
            print("Usage: scons RELEASE=x.y.z release")
            Exit(1)

        dash = tag.find('-')
        if dash > 0:
            version = tag[0:dash]
        else:
            print("** Final releases should be made on GitHub directly "
                  "using a previous pre-release such as a release candidate.\n")
            question = "Are you sure you want to continue? (y/N): "
            answer = None
            while answer not in ["y", "n", ""]:
                answer = input(question).lower().strip()
            if answer != 'y':
                Exit(1)

            version = tag

        try:
            # pylint: disable=consider-using-with
            token = yaml.safe_load(open(os.path.join(os.path.expanduser("~"),
                                                     ".config", "hub"), 'r')
                                   )['github.com'][0]['oauth_token']
        except IOError as excpn:
            if excpn.errno == errno.ENOENT:
                print("You need to install hub (from the hub RPM on EL7) to "
                      "and run it at least once to create an authorization "
                      "token in order to create releases")
                Exit(1)
            raise

        # create a branch for the PR
        branch = 'create-release-{}'.format(tag)
        print("Creating a branch for the PR...")
        repo = pygit2.Repository('.git')
        try:
            base_ref = repo.lookup_reference(
                'refs/remotes/{}/{}'.format(remote_name, base_branch))
        except KeyError:
            print("Branch {}/{} is not a valid branch\n"
                  "See https://github.com/{}/daos/branches".format(
                      remote_name, base_branch, org_name))
            Exit(1)

        # older pygit2 didn't have AlreadyExistsError
        try:
            already_exists_error_exception = pygit2.AlreadyExistsError
        except AttributeError:
            already_exists_error_exception = ValueError

        try:
            repo.branches.create(branch, repo[base_ref.target])
        except already_exists_error_exception:
            print("Branch {} exists locally already.\n"
                  "You need to delete it or rename it to try again.".format(
                      branch))
            Exit(1)

        # and check it out
        print("Checking out branch for the PR...")
        repo.checkout(repo.lookup_branch(branch))

        print("Updating the RPM specfile...")
        if not update_rpm_version(version, tag):
            print("Branch has been left in the created state.  You will have "
                  "to clean it up manually.")
            Exit(1)

        print("Updating the VERSION and TAG files...")
        with open("VERSION", "w") as version_file:
            version_file.write(version + '\n')
        with open("TAG", "w") as version_file:
            version_file.write(tag + '\n')

        print("Committing the changes...")
        # now create the commit
        index = repo.index
        index.read()
        author = repo.default_signature
        committer = repo.default_signature
        summary = "Update version to v{}".format(tag)
        message = "{}\n\n" \
                  "Signed-off-by: {} <{}>".format(summary,
                                                  repo.default_signature.name,
                                                  repo.default_signature.email)
        index.add("utils/rpms/daos.spec")
        index.add("VERSION")
        index.add("TAG")
        index.write()
        tree = index.write_tree()
        repo.create_commit('HEAD', author, committer, message, tree,
                           [repo.head.target])

        # set up authentication callback
        class MyCallbacks(pygit2.RemoteCallbacks):  # pylint: disable=too-few-public-methods
            """ Callbacks for pygit2 """
            @staticmethod
            def credentials(_url, username_from_url, allowed_types):
                """setup credentials"""
                if allowed_types & pygit2.credentials.GIT_CREDTYPE_SSH_KEY:
                    if "SSH_AUTH_SOCK" in os.environ:
                        # Use ssh agent for authentication
                        return pygit2.KeypairFromAgent(username_from_url)
                else:
                    print("No supported credential types allowed by remote end.  SSH_AUTH_SOCK not "
                          "found in your environment.  Are you running an ssh-agent?")
                    Exit(1)
                return None

        # and push it
        print("Pushing the changes to GitHub...")
        remote = repo.remotes[remote_name]
        try:
            remote.push(['refs/heads/{}'.format(branch)],
                        callbacks=MyCallbacks())
        except pygit2.GitError as err:
            print("Error pushing branch: {}".format(err))
            Exit(1)

        print("Creating the PR...")
        # now create a PR for it
        gh_context = github.Github(token)
        try:
            org = gh_context.get_organization(org_name)
            repo = org.get_repo('daos')
        except github.UnknownObjectException:
            # maybe not an organization
            repo = gh_context.get_repo('{}/daos'.format(org_name))
        new_pr = repo.create_pull(title=summary, body="", base=base_branch,
                                  head="{}:{}".format(org_name, branch))

        print("Successfully created PR#{0} for this version "
              "update:\n"
              "https://github.com/{1}/daos/pull/{0}/".format(new_pr.number,
                                                             org_name))

        print("Self-assigning the PR...")
        # self-assign the PR
        new_pr.as_issue().add_to_assignees(
            gh_context.get_user(gh_context.get_user().login))

        print("Done.")

        Exit(0)


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
               'DD_SUBSYS', 'D_IL_MAX_EQ')


def scons():
    """Perform the build"""

    check_for_release_target()

    deps_env = Environment()

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
    # Now save the daos.conf file before attempting to build anything.  This means that options
    # are sticky even if there's a failed build.
    opts.Save(opts_file, deps_env)

    # This will add a final 'DEPS' value to opts but it will not be persistent.
    prereqs.run_build(opts)

    if GetOption('build_deps') == 'only':
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
        env.Install('$PREFIX/lib/daos', ['.build_vars.sh', '.build_vars.json'])
        env.Install('$PREFIX/lib/daos/TESTING/ftest/util', ['site_scons/env_modules.py'])
        env.Install('$PREFIX/lib/daos/TESTING/ftest/', ['ftest.sh'])

    env.Install("$PREFIX/lib64/daos", "VERSION")

    env.Install(os.path.join(conf_dir, 'bash_completion.d'), 'utils/completion/daos.bash')

    build_misc(build_prefix)

    Default(build_prefix)

    # an "rpms" target
    env.Command('rpms', '', 'make -C utils/rpms rpms')

    Help(opts.GenerateHelpText(env), append=True)


if __name__ == "SCons.Script":
    scons()
