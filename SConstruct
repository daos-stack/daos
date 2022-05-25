"""Build DAOS"""
import os
import sys
import platform
import subprocess
import time
import errno
import SCons.Warnings
import daos_build
import compiler_setup
from prereq_tools import PreReqComponent
import stack_analyzer

if sys.version_info.major < 3:
    print(""""Python 2.7 is no longer supported in the DAOS build.
Install python3 version of SCons.   On some platforms this package does not
install the scons binary so your command may need to use scons-3 instead of
scons or you will need to create an alias or script by the same name to
wrap scons-3.""")
    Exit(1)

SCons.Warnings.warningAsException()

try:
    input = raw_input  # pylint: disable=redefined-builtin
except NameError:
    pass


def get_version(env):
    """ Read version from VERSION file """
    with open("VERSION", "r") as version_file:
        version = version_file.read().rstrip()

        (major, minor, fix) = version.split('.')

        env.Append(DAOS_VERSION_MAJOR=major)
        env.Append(DAOS_VERSION_MINOR=minor)
        env.Append(DAOS_VERSION_FIX=fix)

        return version


API_VERSION_MAJOR = "2"
API_VERSION_MINOR = "3"
API_VERSION_FIX = "0"
API_VERSION = "{}.{}.{}".format(API_VERSION_MAJOR, API_VERSION_MINOR,
                                API_VERSION_FIX)


def update_rpm_version(version, tag):
    """ Update the version (and release) in the RPM spec file """
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
                # pylint: disable-next=consider-using-with
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


def is_platform_arm():
    """Detect if platform is ARM"""
    processor = platform.machine()
    arm_list = ["arm", "aarch64", "arm64"]
    if processor.lower() in arm_list:
        return True
    return False


def set_defaults(env, daos_version):
    """set compiler defaults"""
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

    env.Append(API_VERSION_MAJOR=API_VERSION_MAJOR)
    env.Append(API_VERSION_MINOR=API_VERSION_MINOR)
    env.Append(API_VERSION_FIX=API_VERSION_FIX)

    env.Append(CCFLAGS=['-DDAOS_VERSION=\\"' + daos_version + '\\"'])
    env.Append(CCFLAGS=['-DAPI_VERSION=\\"' + API_VERSION + '\\"'])


def build_misc():
    """Build miscellaneous items"""
    # install the configuration files
    SConscript('utils/config/SConscript')

    # install certificate generation files
    SConscript('utils/certs/SConscript')

    # install man pages
    try:
        SConscript('doc/man/SConscript', must_exist=0)
    except SCons.Warnings.MissingSConscriptWarning as _warn:
        print("Missing doc/man/SConscript...")


def scons():  # pylint: disable=too-many-locals,too-many-branches
    """Execute build"""
    if COMMAND_LINE_TARGETS == ['release']:
        try:
            # pylint: disable=import-outside-toplevel
            import pygit2
            import github
            import yaml
        except ImportError:
            print("You need yaml, pygit2 and pygithub python modules to "
                  "create releases")
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
            # pylint: disable-next=consider-using-with
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
                    raise Exception("No supported credential types allowed "
                                    "by remote end.  SSH_AUTH_SOCK not found "
                                    "in your environment.  Are you running an "
                                    "ssh-agent?")
                return None

        # and push it
        print("Pushing the changes to GitHub...")
        remote = repo.remotes[remote_name]
        try:
            remote.push(['refs/heads/{}'.format(branch)],
                        callbacks=MyCallbacks())
        except pygit2.GitError as excpt:
            print("Error pushing branch: {}".format(excpt))
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

    env = Environment(TOOLS=['extra', 'default', 'textfile'])

    # Scons strips out the environment, however to be able to build daos using the interception
    # library we need to add a few things back in.
    if 'LD_PRELOAD' in os.environ:
        # pylint: disable=invalid-sequence-index
        env['ENV']['LD_PRELOAD'] = os.environ['LD_PRELOAD']

        for key in ['D_LOG_FILE', 'DAOS_AGENT_DRPC_DIR', 'D_LOG_MASK', 'DD_MASK', 'DD_SUBSYS']:
            value = os.environ.get(key, None)
            if value is not None:
                env['ENV'][key] = value

    opts_file = os.path.join(Dir('#').abspath, 'daos.conf')
    opts = Variables(opts_file)

    commits_file = os.path.join(Dir('#').abspath, 'utils/build.config')
    if not os.path.exists(commits_file):
        commits_file = None

    platform_arm = is_platform_arm()

    if 'VIRTUAL_ENV' in os.environ:
        env.PrependENVPath('PATH', os.path.join(os.environ['VIRTUAL_ENV'], 'bin'))
        # pylint: disable=invalid-sequence-index
        env['ENV']['VIRTUAL_ENV'] = os.environ['VIRTUAL_ENV']

    prereqs = PreReqComponent(env, opts, commits_file)
    if not GetOption('help') and not GetOption('clean'):
        daos_build.load_mpi_path(env)
    build_prefix = prereqs.get_src_build_dir()
    prereqs.init_build_targets(build_prefix)
    prereqs.load_defaults(platform_arm)
    if prereqs.check_component('valgrind_devel'):
        env.AppendUnique(CPPDEFINES=["D_HAS_VALGRIND"])

    AddOption('--deps-only',
              dest='deps_only',
              action='store_true',
              default=False,
              help='Download and build dependencies only, do not build daos')

    prereqs.add_opts(('GO_BIN', 'Full path to go binary', None))
    opts.Save(opts_file, env)

    res = GetOption('deps_only')
    if res:
        print('Exiting because deps-only was set')
        Exit(0)

    conf_dir = ARGUMENTS.get('CONF_DIR', '$PREFIX/etc')

    env.Alias('install', '$PREFIX')
    daos_version = get_version(env)

    set_defaults(env, daos_version)

    base_env = env.Clone()

    compiler_setup.base_setup(env, prereqs=prereqs)

    args = GetOption('analyze_stack')
    if args is not None:
        analyzer = stack_analyzer.analyzer(env, build_prefix, args)
        analyzer.analyze_on_exit()

    # Export() is handled specially by pylint so do not merge these two lines.
    Export('daos_version', 'API_VERSION', 'env', 'base_env', 'prereqs')
    Export('platform_arm', 'conf_dir')

    # generate targets in specific build dir to avoid polluting the source code
    VariantDir(build_prefix, '.', duplicate=0)
    SConscript('{}/src/SConscript'.format(build_prefix))

    buildinfo = prereqs.get_build_info()
    buildinfo.gen_script('.build_vars.sh')
    buildinfo.save('.build_vars.json')
    # also install to $PREFIX/lib to work with existing avocado test code
    if prereqs.test_requested():
        daos_build.install(env, "lib/daos/",
                           ['.build_vars.sh', '.build_vars.json'])
        env.Install('$PREFIX/lib/daos/TESTING/ftest/util',
                    ['site_scons/env_modules.py'])
        env.Install('$PREFIX/lib/daos/TESTING/ftest/',
                    ['ftest.sh'])

    env.Install("$PREFIX/lib64/daos", "VERSION")

    if prereqs.client_requested():
        api_version = env.Command("%s/API_VERSION" % build_prefix,
                                  "%s/SConstruct" % build_prefix,
                                  "echo %s > $TARGET" % (API_VERSION))
        env.Install("$PREFIX/lib64/daos", api_version)
    env.Install(conf_dir + '/bash_completion.d', ['utils/completion/daos.bash'])

    build_misc()

    Default(build_prefix)

    # an "rpms" target
    env.Command('rpms', '', 'make -C utils/rpms rpms')

    Help(opts.GenerateHelpText(env), append=True)


if __name__ == "SCons.Script":
    scons()
