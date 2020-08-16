"""Build DAOS"""
import sys
import os
import platform
import subprocess
import time
import errno
from SCons.Script import BUILD_TARGETS

try:
    input = raw_input # pylint: disable=redefined-builtin
except NameError:
    pass

sys.path.insert(0, os.path.join(Dir('#').abspath, 'utils'))
import daos_build

DESIRED_FLAGS = ['-Wno-gnu-designator',
                 '-Wno-missing-braces',
                 '-Wno-ignored-attributes',
                 '-Wno-gnu-zero-variadic-macro-arguments',
                 '-Wno-tautological-constant-out-of-range-compare',
                 '-Wno-unused-command-line-argument',
                 '-Wframe-larger-than=4096',
                 ' -mavx2']

# Compiler flags to prevent optimizing out security checks
DESIRED_FLAGS.extend(['-fno-strict-overflow', '-fno-delete-null-pointer-checks',
                      '-fwrapv'])

# Compiler flags for stack hardening
DESIRED_FLAGS.extend(['-fstack-protector-strong', '-fstack-clash-protection'])

PP_ONLY_FLAGS = ['-Wno-parentheses-equality', '-Wno-builtin-requires-header',
                 '-Wno-unused-function']

def run_checks(env):
    """Run all configure time checks"""
    if GetOption('help') or GetOption('clean'):
        return
    cenv = env.Clone()
    cenv.Append(CFLAGS='-Werror')
    if cenv.get("COMPILER") == 'icc':
        cenv.Replace(CC='gcc', CXX='g++')
    config = Configure(cenv)

    if config.CheckHeader('stdatomic.h'):
        env.AppendUnique(CPPDEFINES=['HAVE_STDATOMIC=1'])

    config.Finish()

def get_version():
    """ Read version from VERSION file """
    with open("VERSION", "r") as version_file:
        return version_file.read().rstrip()

DAOS_VERSION = get_version()
API_VERSION = "0.9.0"

def update_rpm_version(version, tag):
    """ Update the version (and release) in the RPM specfile """
    spec = open("utils/rpms/daos.spec", "r").readlines()
    current_version = 0
    release = 0
    for line_num, line in enumerate(spec):
        if line.startswith("Version:"):
            current_version = line[line.rfind(' ')+1:].rstrip()
            if version < current_version:
                print("You cannot create a new version ({}) lower than the RPM "
                      "spec file has currently ({})".format(version,
                                                            current_version))
                return False
            if version > current_version:
                spec[line_num] = "Version:       {}\n".format(version)
        if line.startswith("Release:"):
            if version == current_version:
                current_release = int(line[line.rfind(' ')+1:line.find('%')])
                release = current_release + 1
            else:
                release = 1
            spec[line_num] = "Release:       {}%{{?relval}}%{{?dist}}\n".\
                             format(release)
        if line == "%changelog\n":
            try:
                packager = subprocess.Popen(
                    'rpmdev-packager', stdout=subprocess.PIPE).communicate(
                    )[0].strip().decode('UTF-8')
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
                        u'* {} {} - {}-{}\n'.format(date_str,
                                                    packager,
                                                    version,
                                                    release))
            break
    open("utils/rpms/daos.spec", "w").writelines(spec)

    return True

def is_platform_arm():
    """Detect if platform is ARM"""
    processor = platform.machine()
    arm_list = ["arm", "aarch64", "arm64"]
    if processor.lower() in arm_list:
        return True
    return False

def set_defaults(env):
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

    env.Append(CCFLAGS=['-g', '-Wshadow', '-Wall', '-Wno-missing-braces',
                        '-fpic', '-D_GNU_SOURCE', '-DD_LOG_V2'])
    env.Append(CCFLAGS=['-DDAOS_VERSION=\\"' + DAOS_VERSION + '\\"'])
    env.Append(CCFLAGS=['-DAPI_VERSION=\\"' + API_VERSION + '\\"'])
    env.Append(CCFLAGS=['-DCMOCKA_FILTER_SUPPORTED=0'])
    if env.get('BUILD_TYPE') == 'debug':
        if env.get("COMPILER") == 'gcc':
            env.AppendUnique(CCFLAGS=['-Og'])
        else:
            env.AppendUnique(CCFLAGS=['-O0'])
    else:
        if env.get('BUILD_TYPE') == 'release':
            env.Append(CCFLAGS=['-DDAOS_BUILD_RELEASE'])
        env.AppendUnique(CCFLAGS=['-O2', '-D_FORTIFY_SOURCE=2'])

    env.AppendIfSupported(CCFLAGS=DESIRED_FLAGS)

    if GetOption("preprocess"):
        #could refine this but for now, just assume these warnings are ok
        env.AppendIfSupported(CCFLAGS=PP_ONLY_FLAGS)

def preload_prereqs(prereqs):
    """Preload prereqs specific to platform"""

    prereqs.define('cmocka', libs=['cmocka'], package='libcmocka-devel')
    prereqs.define('readline', libs=['readline', 'history'],
                   package='readline')
    reqs = ['argobots', 'pmdk', 'cmocka', 'ofi', 'hwloc', 'mercury', 'boost',
            'uuid', 'crypto', 'fuse', 'protobufc', 'json-c']
    if not is_platform_arm():
        reqs.extend(['spdk', 'isal', 'isal_crypto'])
    prereqs.load_definitions(prebuild=reqs)

def scons(): # pylint: disable=too-many-locals
    """Execute build"""
    if COMMAND_LINE_TARGETS == ['release']:
        try:
            import pygit2
            import github
            import yaml
        except ImportError:
            print("You need yaml, pygit2 and pygithub python modules to "
                  "create releases")
            exit(1)

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
            exit(1)

        dash = tag.find('-')    # pylint: disable=no-member
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
                exit(1)

            version = tag

        try:
            token = yaml.safe_load(open(os.path.join(os.path.expanduser("~"),
                                                     ".config", "hub"), 'r')
                                  )['github.com'][0]['oauth_token']
        except IOError as excpn:
            if excpn.errno == errno.ENOENT:
                print("You need to install hub (from the hub RPM on EL7) to "
                      "and run it at least once to create an authorization "
                      "token in order to create releases")
                exit(1)
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
            exit(1)

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
            exit(1)

        # and check it out
        print("Checking out branch for the PR...")
        repo.checkout(repo.lookup_branch(branch))

        print("Updating the RPM specfile...")
        if not update_rpm_version(version, tag):
            print("Branch has been left in the created state.  You will have "
                  "to clean it up manually.")
            exit(1)

        print("Updating the API_VERSION, VERSION and TAG files...")
        with open("API_VERSION", "w") as version_file:
            version_file.write(API_VERSION + '\n')
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
        # pylint: disable=no-member
        message = "{}\n\n" \
                  "Signed-off-by: {} <{}>".format(summary,
                                                  repo.default_signature.name,
                                                  repo.default_signature.email)
        # pylint: enable=no-member
        index.add("utils/rpms/daos.spec")
        index.add("API_VERSION")
        index.add("VERSION")
        index.add("TAG")
        index.write()
        tree = index.write_tree()
        # pylint: disable=no-member
        repo.create_commit('HEAD', author, committer, message, tree,
                           [repo.head.target])
        # pylint: enable=no-member

        # set up authentication callback
        class MyCallbacks(pygit2.RemoteCallbacks): # pylint: disable=too-few-public-methods
            """ Callbacks for pygit2 """
            @staticmethod
            def credentials(_url, username_from_url, allowed_types): # pylint: disable=method-hidden
                """setup credentials"""
                if allowed_types & pygit2.credentials.GIT_CREDTYPE_SSH_KEY:
                    if "SSH_AUTH_SOCK" in os.environ:
                        # Use ssh agent for authentication
                        return pygit2.KeypairFromAgent(username_from_url)
                    #else:
                    # need to determine if key is passphrase protected and ask
                    # for the passphrase in order to use this method
                    #    ssh_key = os.path.join(os.path.expanduser("~"),
                    #                           ".ssh", "id_rsa")
                    #    return pygit2.Keypair("git", ssh_key + ".pub",
                    #                          ssh_key, "")
                #elif allowed_types & pygit2.credentials.GIT_CREDTYPE_USERNAME:
                # this is not really useful in the GitHub context
                #    return pygit2.Username("git")
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
            exit(1)

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

        exit(0)

    sys.path.insert(0, os.path.join(Dir('#').abspath, 'utils/sl'))
    from prereq_tools import PreReqComponent

    env = Environment(TOOLS=['extra', 'default'])

    if os.path.exists("daos_m.conf"):
        os.rename("daos_m.conf", "daos.conf")

    opts_file = os.path.join(Dir('#').abspath, 'daos.conf')
    opts = Variables(opts_file)

    commits_file = os.path.join(Dir('#').abspath, 'utils/build.config')
    if not os.path.exists(commits_file):
        commits_file = None

    prereqs = PreReqComponent(env, opts, commits_file)
    if not GetOption('help') and not GetOption('clean'):
        daos_build.load_mpi_path(env)
    preload_prereqs(prereqs)
    if prereqs.check_component('valgrind_devel'):
        env.AppendUnique(CPPDEFINES=["DAOS_HAS_VALGRIND"])

    run_checks(env)

    prereqs.add_opts(('GO_BIN', 'Full path to go binary', None))
    opts.Save(opts_file, env)

    CONF_DIR = ARGUMENTS.get('CONF_DIR', '$PREFIX/etc')

    env.Alias('install', '$PREFIX')
    platform_arm = is_platform_arm()
    Export('DAOS_VERSION', 'API_VERSION',
           'env', 'prereqs', 'platform_arm',
           'CONF_DIR')

    if env['PLATFORM'] == 'darwin':
        # generate .so on OSX instead of .dylib
        env.Replace(SHLIBSUFFIX='.so')

    set_defaults(env)

    build_prefix = prereqs.get_src_build_dir()

    # generate targets in specific build dir to avoid polluting the source code
    VariantDir(build_prefix, '.', duplicate=0)
    SConscript('{}/src/SConscript'.format(build_prefix))

    buildinfo = prereqs.get_build_info()
    buildinfo.gen_script('.build_vars.sh')
    buildinfo.save('.build_vars.json')
    # also install to $PREFIX/lib to work with existing avocado test code
    daos_build.install(env, "lib/daos/", ['.build_vars.sh', '.build_vars.json'])
    env.Install("$PREFIX/lib64/daos", "VERSION")
    env.Install("$PREFIX/lib64/daos", "API_VERSION")

    env.Install('$PREFIX/etc', ['utils/memcheck-daos-client.supp'])
    env.Install('$PREFIX/lib/daos/TESTING/ftest/util',
                ['utils/sl/env_modules.py'])
    env.Install('$PREFIX/lib/daos/TESTING/ftest/',
                ['ftest.sh'])

    # install the configuration files
    SConscript('utils/config/SConscript')

    # install certificate generation files
    SConscript('utils/certs/SConscript')

    # install man pages
    SConscript('doc/man/SConscript')

    Default(build_prefix)
    Depends('install', build_prefix)

    # an "rpms" target
    env.Command('rpms', '', 'make -C utils/rpms rpms')

    try:
        #if using SCons 2.4+, provide a more complete help
        Help(opts.GenerateHelpText(env), append=True)
    except TypeError:
        Help(opts.GenerateHelpText(env))

if __name__ == "SCons.Script":
    scons()
