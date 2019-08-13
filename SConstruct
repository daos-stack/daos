"""Build DAOS"""
import sys
import os
import platform
import subprocess
import locale
import time
from SCons.Script import BUILD_TARGETS

sys.path.insert(0, os.path.join(Dir('#').abspath, 'utils'))

DESIRED_FLAGS = ['-Wno-gnu-designator',
                 '-Wno-missing-braces',
                 '-Wno-ignored-attributes',
                 '-Wno-gnu-zero-variadic-macro-arguments',
                 '-Wno-tautological-constant-out-of-range-compare',
                 '-Wframe-larger-than=4096']

PP_ONLY_FLAGS = ['-Wno-parentheses-equality', '-Wno-builtin-requires-header',
                 '-Wno-unused-function']

def get_version():

    def read_from_file():
        with open("VERSION", "r") as version_file:
            return version_file.read()

    try:
        import pygit2
    except ImportError:
        version = read_from_file()
        print "Building verison {} (version info from file)".format(
            version.rstrip())
        return version

    try:
        repo = pygit2.Repository('.git')
        # TODO: when newer annotated tags get created, remove the
        # describe_strategy below
        tag = repo.describe(describe_strategy=pygit2.GIT_DESCRIBE_TAGS).split('-')[0]
        version = tag[1:].split('.')
        while len(version) < 3:
            version.append(u'0')
        version = '.'.join(version)
        print "Building verison {} (version info from git)".format(version)
        return version
    except pygit2.GitError:
        return read_from_file()

DAOS_VERSION = get_version()

def update_rpm_version(version):
    spec = open("utils/rpms/daos.spec", "r").readlines()
    for line_num, line in enumerate(spec):
        if line.startswith("Version:"):
            spec[line_num] = "Version:       {}\n".format(version)
        if line.startswith("Release:"):
            spec[line_num] = "Release:       1%{?relval}%{?dist}\n"
        if line == "%changelog\n":
            try:
                packager = subprocess.Popen(
                    'rpmdev-packager', stdout=subprocess.PIPE).communicate(
                    )[0].strip()
            except OSError:
                packager = "John Doe <john@doe.com>"
                print "Package rpmdevtools is missing, using default " \
                      "name: {0}.".format(packager)
            date_str = time.strftime('%a %b %d %Y', time.gmtime())
            encoding = locale.getpreferredencoding()
            spec.insert(line_num + 1, "\n")
            spec.insert(line_num + 1,
                        "- Version bump up to {}\n".format(version))
            spec.insert(line_num + 1,
                        u'* {} {} - {}-1\n'.format(date_str,
                                                   packager,
                                                   version))
            break
    open("utils/rpms/daos.spec", "w").writelines(spec)

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

    env.Append(CCFLAGS=['-g', '-Wshadow', '-Wall', '-Wno-missing-braces',
                        '-fpic', '-D_GNU_SOURCE', '-DD_LOG_V2'])
    env.Append(CCFLAGS=['-O2', '-DDAOS_VERSION=\\"' + DAOS_VERSION + '\\"'])
    env.AppendIfSupported(CCFLAGS=DESIRED_FLAGS)
    if GetOption("preprocess"):
        #could refine this but for now, just assume these warnings are ok
        env.AppendIfSupported(CCFLAGS=PP_ONLY_FLAGS)

def preload_prereqs(prereqs):
    """Preload prereqs specific to platform"""
    prereqs.define('cmocka', libs=['cmocka'], package='libcmocka-devel')
    prereqs.define('readline', libs=['readline', 'history'],
                   package='readline')
    reqs = ['cart', 'argobots', 'pmdk', 'cmocka',
            'uuid', 'crypto', 'fuse', 'protobufc']
    if not is_platform_arm():
        reqs.extend(['spdk', 'isal'])
    prereqs.load_definitions(prebuild=reqs)

def scons():
    if COMMAND_LINE_TARGETS == ['release']:
        import pygit2
        vars = Variables()
        vars.Add('RELEASE', 'Set to the release version to make', None)
        env = Environment(variables = vars)
        try:
            version = env['RELEASE']
        except KeyError:
            print "Usage: scons RELEASE=x.y.z release"
            exit(1)

        with open("VERSION", "w") as version_file:
            version_file.write(version + '\n')

        update_rpm_version(version)

        repo = pygit2.Repository('.git')
        index = repo.index
        index.read()
        author = repo.default_signature
        committer = repo.default_signature
        message = "DAOS-2172 version: bump version to v{}\n".format(version)
        index.add("utils/rpms/daos.spec")
        index.add("VERSION")
        index.write()
        tree = index.write_tree()
        repo.create_commit('HEAD', author, committer, message, tree,
                           [repo.head.target])
        repo.create_tag("v{}".format(version),
                        repo.revparse_single('HEAD').oid.hex,
                        pygit2.GIT_OBJ_COMMIT, author,
                        message)
        exit(0)

    """Execute build"""
    if os.path.exists('scons_local'):
        try:
            sys.path.insert(0, os.path.join(Dir('#').abspath, 'scons_local'))
            from prereq_tools import PreReqComponent
            print ('Using scons_local build')
        except ImportError:
            print ('Using traditional build')

    env = Environment(TOOLS=['extra', 'default'])

    if os.path.exists("daos_m.conf"):
        os.rename("daos_m.conf", "daos.conf")

    opts_file = os.path.join(Dir('#').abspath, 'daos.conf')
    opts = Variables(opts_file)

    commits_file = os.path.join(Dir('#').abspath, 'utils/build.config')
    if not os.path.exists(commits_file):
        commits_file = None

    prereqs = PreReqComponent(env, opts, commits_file)
    preload_prereqs(prereqs)
    opts.Save(opts_file, env)

    env.Alias('install', '$PREFIX')
    platform_arm = is_platform_arm()
    Export('DAOS_VERSION', 'env', 'prereqs', 'platform_arm')

    if env['PLATFORM'] == 'darwin':
        # generate .so on OSX instead of .dylib
        env.Replace(SHLIBSUFFIX='.so')

    set_defaults(env)

    # generate targets in specific build dir to avoid polluting the source code
    VariantDir('build', '.', duplicate=0)
    SConscript('build/src/SConscript')

    buildinfo = prereqs.get_build_info()
    buildinfo.gen_script('.build_vars.sh')
    buildinfo.save('.build_vars.json')
    env.InstallAs("$PREFIX/TESTING/.build_vars.sh", ".build_vars.sh")
    env.InstallAs("$PREFIX/TESTING/.build_vars.json", ".build_vars.json")
    env.InstallAs("$PREFIX/lib/daos/VERSION", "VERSION")

    # install the configuration files
    SConscript('utils/config/SConscript')

    # install certificate generation files
    SConscript('utils/certs/SConscript')

    Default('build')
    Depends('install', 'build')

    try:
        #if using SCons 2.4+, provide a more complete help
        Help(opts.GenerateHelpText(env), append=True)
    except TypeError:
        Help(opts.GenerateHelpText(env))

if __name__ == "SCons.Script":
    scons()
