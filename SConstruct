#!python

import sys
import os
from SCons.Script import BUILD_TARGETS

BUILD_TARGETS.append('fixtest')


have_scons_local=False
if os.path.exists('scons_local'):
        try:
                sys.path.insert(0, os.path.join(Dir('#').abspath,
				'scons_local'))
                from prereq_tools import PreReqComponent
                from build_info import BuildInfo
                have_scons_local=True
                print ('Using scons_local build')
        except ImportError:
                print ('Using traditional build')
                pass

env = Environment()

OPTS_FILE = os.path.join(Dir('#').abspath, 'daos_m.conf')
OPTS = Variables(OPTS_FILE)

COMMITS_FILE = os.path.join(Dir('#').abspath, 'utils/build.config')
if not os.path.exists(COMMITS_FILE):
    COMMITS_FILE = None

PREREQS = PreReqComponent(env, OPTS, COMMITS_FILE)
PREREQS.define('cmocka', libs=['cmocka'], package='libcmocka-devel')
PREREQS.preload(os.path.join(Dir('#').abspath, 'scons_local', 'components.py'),
                prebuild=['ompi', 'cart', 'argobots', 'nvml', 'cmocka', 'uuid',
                          'crypto'])
OPTS.Save(OPTS_FILE, env)

# Define this now, and then the individual components can import this
# through PREREQS when they need it.
env.Append(CPPDEFINES={'DAOS_HAS_NVML' : '1'})

env.Alias('install', '$PREFIX')

DAOS_VERSION = "0.0.2"
Export('DAOS_VERSION')

if env['PLATFORM'] == 'darwin':
	# generate .so on OSX instead of .dylib
	env['SHLIBSUFFIX'] = '.so'

# Compiler options
env.Append(CCFLAGS = ['-g', '-Wall', '-Werror', '-Wno-missing-braces',
		      '-fpic', '-D_GNU_SOURCE'])
env.Append(CCFLAGS = ['-O2', '-DDAOS_VERSION=\\"' + DAOS_VERSION + '\\"'])

# generate targets in specific build dir to avoid polluting the source code
VariantDir('build', '.', duplicate=0)
SConscript('build/src/SConscript', exports=['env', 'PREREQS'])

BUILDINFO = PREREQS.get_build_info()
BUILDINFO.gen_script('.build_vars.sh')
BUILDINFO.save('.build_vars.json')
env.InstallAs("$PREFIX/TESTING/.build_vars.sh", ".build_vars.sh")
env.InstallAs("$PREFIX/TESTING/.build_vars.json", ".build_vars.json")

# install the test_runner code from scons_local
SConscript('build/scons_local/test_runner/SConscript', exports=['env', 'PREREQS'])

# install the build verification tests
SConscript('utils/bvtest/scripts/SConscript', exports=['env', 'PREREQS'])

env.Command("fixtest", "./utils/bvtest/OrteRunner.py",
            [
                    Copy("$PREFIX/TESTING/test_runner/", "./utils/bvtest/OrteRunner.py", False)
            ])

Default('build')
Depends('install', 'build')
Depends('fixtest', 'install')

try:
    #if using SCons 2.4+, provide a more complete help
    Help(OPTS.GenerateHelpText(env), append=True)
except TypeError:
    Help(OPTS.GenerateHelpText(env))
