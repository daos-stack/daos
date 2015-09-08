#!python

env = Environment()

# Compiler options
env.Append(CCFLAGS = ['-g', '-Wall', '-D_GNU_SOURCE', '-fpic'])
env.Append(CCFLAGS = ['-O2'])
env.Append(CPPPATH = ['#/include'])

# Builders
SConscript('common/SConscript', exports='env')
SConscript('dsr/SConscript',  exports='env')
