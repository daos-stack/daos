#!python

import sys
import os

have_scons_local=False
if os.path.exists('scons_local'):
        try:
                sys.path.insert(0, os.path.join(Dir('#').abspath,
				'scons_local'))
                from prereq_tools import PreReqComponent
                have_scons_local=True
                print ('Using scons_local build')
        except ImportError:
                print ('Using traditional build')
                pass

env = Environment()

config = Configure(env)
for required_lib in ['cgroup', 'uuid']:
        if not config.CheckLib(required_lib):
                config.Finish()
                exit(1)

if not config.CheckLib('numa'):
        config.Finish()
        print ('for libnuma install numactl-devel package')
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
        if config.CheckHeader('libpmemobj.h'):
                env.Append(CPPDEFINES={'DAOS_HAS_NVML' : '1'})
        else:
                env.Append(CPPDEFINES={'DAOS_HAS_NVML' : '0'})

config.Finish()

if env['PLATFORM'] == 'darwin':
	# generate .so on OSX instead of .dylib
	env['SHLIBSUFFIX'] = '.so'

# Compiler options
env.Append(CCFLAGS = ['-g', '-Wall', '-Werror', '-fpic', '-D_GNU_SOURCE'])
env.Append(CCFLAGS = ['-O2'])

# All libraries will be generated under build/lib and binaries under build/bin
env.Append(LIBPATH = ['#/build/lib', '#/build/lib/daos_srv'])
env.Append(PATH = ['#/build/bin'])

# generate targets in specific build dir to avoid polluting the source code
SConscript('src/SConscript', exports=['env', 'PREREQS'], variant_dir='build',
	   duplicate=0)
