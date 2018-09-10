"""Build DAOS"""
import sys
import os
import platform
from SCons.Script import BUILD_TARGETS

sys.path.insert(0, os.path.join(Dir('#').abspath, 'utils'))

DESIRED_FLAGS = ['-Wno-gnu-designator',
                 '-Wno-missing-braces',
                 '-Wno-gnu-zero-variadic-macro-arguments',
                 '-Wno-tautological-constant-out-of-range-compare']

DAOS_VERSION = "0.0.2"

def is_platform_arm():
    """Detect if platform is ARM"""
    processor = platform.machine()
    arm_list = ["arm", "aarch64", "arm64"]
    if processor.lower() in arm_list:
        return True
    return False

def preload_prereqs(prereqs):
    """Preload prereqs specific to platform"""
    prereqs.define('cmocka', libs=['cmocka'], package='libcmocka-devel')
    prereqs.define('readline', libs=['readline', 'history'],
                   package='readline')
    components = os.path.join(Dir('#').abspath, 'scons_local', 'components.py')
    reqs = ['cart', 'argobots', 'pmdk', 'cmocka',
            'uuid', 'crypto', 'fuse']
    if not is_platform_arm():
        reqs.extend(['spdk', 'isal'])
    prereqs.preload(components, prebuild=reqs)
    return prereqs

def scons():
    """Execute build"""
    BUILD_TARGETS.append('fixtest')
    if os.path.exists('scons_local'):
        try:
            sys.path.insert(0, os.path.join(Dir('#').abspath, 'scons_local'))
            from prereq_tools import PreReqComponent
            print ('Using scons_local build')
        except ImportError:
            print ('Using traditional build')

    env = Environment(TOOLS=['default', 'go'])

    opts_file = os.path.join(Dir('#').abspath, 'daos_m.conf')
    opts = Variables(opts_file)

    commits_file = os.path.join(Dir('#').abspath, 'utils/build.config')
    if not os.path.exists(commits_file):
        commits_file = None

    prereqs = PreReqComponent(env, opts, commits_file)
    prereqs = preload_prereqs(prereqs)
    opts.Save(opts_file, env)

    # Define this now, and then the individual components can import this
    # through prereqs when they need it.
    env.Append(CPPDEFINES={'DAOS_HAS_PMDK' : '1'})

    env.Alias('install', '$PREFIX')
    platform_arm = is_platform_arm()
    Export('DAOS_VERSION', 'env', 'prereqs', 'platform_arm')

    if env['PLATFORM'] == 'darwin':
        # generate .so on OSX instead of .dylib
        env.Replace(SHLIBSUFFIX='.so')

    # Compiler options
    env.Append(CCFLAGS=['-g', '-Wshadow', '-Wall', '-Wno-missing-braces',
                        '-fpic', '-D_GNU_SOURCE'])
    env.Append(CCFLAGS=['-O2', '-DDAOS_VERSION=\\"' + DAOS_VERSION + '\\"'])
    env.AppendIfSupported(CCFLAGS=DESIRED_FLAGS)

    # generate targets in specific build dir to avoid polluting the source code
    VariantDir('build', '.', duplicate=0)
    SConscript('build/src/SConscript')

    buildinfo = prereqs.get_build_info()
    buildinfo.gen_script('.build_vars.sh')
    buildinfo.save('.build_vars.json')
    env.InstallAs("$PREFIX/TESTING/.build_vars.sh", ".build_vars.sh")
    env.InstallAs("$PREFIX/TESTING/.build_vars.json", ".build_vars.json")

    # install the test_runner code from scons_local
    SConscript('build/scons_local/test_runner/SConscript')

    # install the build verification tests
    SConscript('utils/bvtest/scripts/SConscript')

    env.Command("fixtest", "./utils/bvtest/OrteRunner.py",
                [Copy("$PREFIX/TESTING/test_runner/",
                      "./utils/bvtest/OrteRunner.py")])

    Default('build')
    Depends('install', 'build')
    Depends('fixtest', 'install')

    try:
        #if using SCons 2.4+, provide a more complete help
        Help(opts.GenerateHelpText(env), append=True)
    except TypeError:
        Help(opts.GenerateHelpText(env))

if __name__ == "SCons.Script":
    scons()
