"""Build DAOS"""
import sys
import os
import platform
from SCons.Script import BUILD_TARGETS
from prereq_tools import GitRepoRetriever

sys.path.insert(0, os.path.join(Dir('#').abspath, 'utils'))

DESIRED_FLAGS = ['-Wno-gnu-designator',
                 '-Wno-missing-braces',
                 '-Wno-ignored-attributes',
                 '-Wno-gnu-zero-variadic-macro-arguments',
                 '-Wno-tautological-constant-out-of-range-compare',
                 '-Wframe-larger-than=4096']

PP_ONLY_FLAGS = ['-Wno-parentheses-equality', '-Wno-builtin-requires-header',
                 '-Wno-unused-function']

DAOS_VERSION = "0.6.0"

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
    prereqs.define('mchecksum',
                   retriever=GitRepoRetriever("https://github.com/mercury-hpc/"
                                              "mchecksum.git"),
                   commands=['cmake -DBUILD_SHARED_LIBS=ON $MCHECKSUM_SRC '
                             '-DBUILD_TESTING=ON '
                             '-DCMAKE_INSTALL_PREFIX=$MCHECKSUM_PREFIX '
                             '-DMCHECKSUM_ENABLE_COVERAGE=OFF '
                             '-DMCHECKSUM_ENABLE_VERBOSE_ERROR=ON '
                             '-DMCHECKSUM_USE_ZLIB=OFF '
                             '-DCMAKE_INSTALL_RPATH=$MCHECKSUM_PREFIX/lib '
                             '-DCMAKE_INSTALL_RPATH_USE_LINK_PATH=TRUE ',
                             'make $JOBS_OPT', 'make install'],
                   libs=['mchecksum'],
                   out_of_src_build=True)

    # TODO: Remove when able to rev scons_local submodule
    prereqs.define('isal_crypto',
                retriever=GitRepoRetriever("https://github.com/intel/"
                                           "isa-l_crypto"),
                commands=['./autogen.sh ',
                          './configure --prefix=$ISAL_CRYPTO_PREFIX '
                          '--libdir=$ISAL_CRYPTO_PREFIX/lib',
                          'make $JOBS_OPT', 'make install'],
                libs=['isal_crypto'])

    prereqs.define('cmocka', libs=['cmocka'], package='libcmocka-devel')
    prereqs.define('readline', libs=['readline', 'history'],
                   package='readline')
    reqs = ['cart', 'argobots', 'pmdk', 'cmocka',
            'uuid', 'crypto', 'fuse', 'protobufc', 'mchecksum']
    if not is_platform_arm():
        reqs.extend(['spdk', 'isal', 'isal_crypto'])
    prereqs.load_definitions(prebuild=reqs)

def scons():
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
