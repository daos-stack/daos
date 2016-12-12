#!python

import sys
import os

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

config = Configure(env)
for required_lib in ['uuid', 'cmocka']:
        if not config.CheckLib(required_lib):
                config.Finish()
                exit(1)

if not config.CheckLib('crypto'):
        config.Finish()
        print ('for libcrypto install openssl-devel package')
        exit(1)

for required_header in ['openssl/md5.h']:
        if not config.CheckHeader(required_header):
                config.Finish()
                exit(1)

if have_scons_local:
        OPTS_FILE = os.path.join(Dir('#').abspath, 'daos_m.conf')
        OPTS = Variables(OPTS_FILE)

        PREREQS = PreReqComponent(env, OPTS)
        PREREQS.preload(os.path.join(Dir('#').abspath,
                                     'scons_local',
                             'components.py'))
        OPTS.Save(OPTS_FILE, env)
        # Define this now, and then the individual compenents can import this
        # through PREREQS when they need it.
        env.Append(CPPDEFINES={'DAOS_HAS_NVML' : '1'})
else:
        PREREQS = None
        env.Replace(PREFIX=Dir('#build').abspath)

        if config.CheckHeader('libpmemobj.h'):
                env.Append(CPPDEFINES={'DAOS_HAS_NVML' : '1'})
        else:
                env.Append(CPPDEFINES={'DAOS_HAS_NVML' : '0'})

env.Alias('install', '$PREFIX')
config.Finish()

DAOS_VERSION = "0.0.2"
Export('DAOS_VERSION')

if env['PLATFORM'] == 'darwin':
	# generate .so on OSX instead of .dylib
	env['SHLIBSUFFIX'] = '.so'

# Compiler options
env.Append(CCFLAGS = ['-g', '-Wall', '-Werror', '-fpic', '-D_GNU_SOURCE'])
env.Append(CCFLAGS = ['-O2', '-DDAOS_VERSION=\\"' + DAOS_VERSION + '\\"'])

# generate targets in specific build dir to avoid polluting the source code
VariantDir('build', '.', duplicate=0)
SConscript('build/src/SConscript', exports=['env', 'PREREQS'])

if have_scons_local:
    BUILDINFO = PREREQS.get_build_info()
    BUILDINFO.gen_script('.build_vars.sh')
    BUILDINFO.save('.build_vars.json')
    env.InstallAs("$PREFIX/TESTING/.build_vars.sh", ".build_vars.sh")
    env.InstallAs("$PREFIX/TESTING/.build_vars.json", ".build_vars.json")

Default('build')
Depends('install', 'build')
