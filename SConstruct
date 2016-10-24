#!python
# Copyright (C) 2016 Intel Corporation
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted for any purpose (including commercial purposes)
# provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice,
#    this list of conditions, and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions, and the following disclaimer in the
#    documentation and/or materials provided with the distribution.
#
# 3. In addition, redistributions of modified forms of the source or binary
#    code must carry prominent notices stating that the original code was
#    changed and the date of the change.
#
#  4. All publications or advertising materials mentioning features or use of
#     this software are asked, but not required, to acknowledge that it was
#     developed by Intel Corporation and credit the contributors.
#
# 5. Neither the name of Intel Corporation, nor the name of any Contributor
#    may be used to endorse or promote products derived from this software
#    without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
# DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
# ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
# THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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

    env.InstallAs('$PREFIX/etc/cci.ini', 'utils/cci/cci.ini')

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
    config.Finish()

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
    SConscript('%s/test/SConscript' % arch_dir, exports=['env', 'prereqs'])
    SConscript('%s/scons_local/test_runner/SConscript' % arch_dir,
               exports=['env', 'prereqs'])

    env.Install('$PREFIX/etc', ['utils/memcheck-cart.supp'])

    # Put this after all SConscript calls so that any imports they require can
    # be included.
    save_build_info(env, prereqs, platform)

    try:
        #if using SCons 2.4+, provide a more complete help
        Help(opts.GenerateHelpText(env), append=True)
    except TypeError:
        Help(opts.GenerateHelpText(env))

if __name__ == "SCons.Script":
    scons()
