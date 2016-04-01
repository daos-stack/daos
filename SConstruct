#!python

import sys
import os

env = Environment()

# Pass TERM through to the environment to allow colour output from gcc
term = os.environ.get("TERM")
if term:
        real_env = env['ENV']
        real_env["TERM"] = term
        env.Replace(ENV=real_env)

# manage build log
bldir = 'build'
bldlog = bldir + '/build.log'
if env.GetOption('clean'):
        # cleanup build output file
        if os.path.exists(bldlog):
                os.remove(bldlog)
else:
        # redirect build output to a file
        if not os.path.exists(bldir):
                os.makedirs(bldir)
        sys.stdout = os.popen('tee ' + bldlog , 'w')
        sys.stderr = sys.stdout

if env['PLATFORM'] == 'darwin':
	# generate .so on OSX instead of .dylib
	env['SHLIBSUFFIX'] = '.so'

# Compiler options
env.Append(CCFLAGS = ['-g', '-Wall', '-D_GNU_SOURCE', '-fpic'])
env.Append(CCFLAGS = ['-O2'])

# All libraries will be installed under build/lib
env.Append(LIBPATH = ['#/build/lib'])

# generate targets in specific build dir to avoid polluting the source code
SConscript('src/SConscript', exports='env', variant_dir='build', duplicate=0)
