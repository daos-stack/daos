#!python
"""Build CaRT components"""

import sys
import os

sys.path.insert(0, os.path.join(Dir('#').abspath, "scons_local"))
try:
    from prereq_tools import PreReqComponent
except ImportError:
    raise ImportError \
          ("\'prereq_tools\' module not found; run \'git submodule update\'")

def save_build_info(env, prereqs, platform):
    """Save the build information"""

    build_info = prereqs.get_build_info()

    #Save the build info locally
    json_build_vars = '.build_vars-%s.json' % platform
    sh_build_vars = '.build_vars-%s.sh' % platform
    build_info.gen_script(sh_build_vars)
    build_info.save(json_build_vars)

    #Install the build info to the testing directory
    env.InstallAs('$PREFIX/TESTING/.build_vars.sh', sh_build_vars)
    env.InstallAs('$PREFIX/TESTING/.build_vars.json', json_build_vars)

def scons():
    """Scons function"""

    platform = os.uname()[0]
    env = DefaultEnvironment()
    config = Configure(env)
    if not config.CheckLib('crypto'):
        config.Finish()
        print "for libcrypto install openssl-devel package"
        exit(1)

    for required_header in ['openssl/md5.h']:
        if not config.CheckHeader(required_header):
            config.Finish()
            exit(1)

    opts_file = os.path.join(Dir('#').abspath, 'cart-%s.conf' % platform)
    opts = Variables(opts_file)

    if os.path.exists('daos_m.conf') and not os.path.exists(opts_file):
        print 'Renaming legacy conf file'
        os.rename('daos_m.conf', opts_file)

    prereqs = PreReqComponent(env, opts, arch=platform)
    prereqs.preload(os.path.join(Dir('#').abspath, 'scons_local',
                                 'components.py'),
                    prebuild=['ompi'])
    opts.Save(opts_file, env)
    # Define this now, and then the individual compenents can import this
    # through PREREQS when they need it.
    env.Append(CPPDEFINES={'DAOS_HAS_NVML' : '1'})
    env.Alias('install', '$PREFIX')
    config.Finish()

    if platform == 'Darwin':
        # generate .so on OSX instead of .dylib
        env.Append(SHLIBSUFFIX='.so')

    # Compiler options
    env.Append(CCFLAGS=['-g', '-Wall', '-Werror', '-fpic', '-D_GNU_SOURCE'])
    env.Append(CCFLAGS=['-O2'])

    # generate targets in specific build dir to avoid polluting the source code
    arch_dir = 'build/%s' % platform
    VariantDir(arch_dir, '.', duplicate=0)
    SConscript('%s/src/SConscript' % arch_dir, exports=['env', 'prereqs'])
    SConscript('%s/scons_local/test_runner/SConscript' % arch_dir,
               exports=['env', 'prereqs'])

    # Put this after all SConscript calls so that any imports they require can
    # be included.
    save_build_info(env, prereqs, platform)

    Depends('install', 'build')

    try:
        #if using SCons 2.4+, provide a more complete help
        Help(opts.GenerateHelpText(env), append=True)
    except TypeError:
        Help(opts.GenerateHelpText(env))

if __name__ == "SCons.Script":
    scons()
