#!python

import sys
import os

env = Environment()

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

# Compiler options
env.Append(CCFLAGS = ['-g', '-Wall', '-D_GNU_SOURCE', '-fpic'])
env.Append(CCFLAGS = ['-O2'])

# generate targets in specific build dir to avoid polluting the source code
SConscript('src/SConscript', exports='env', variant_dir='build', duplicate=0)
