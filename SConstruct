# Copyright (C) 2016-2017 Intel Corporation
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

import os
import sys

sys.path.insert(0, os.path.join(Dir('#').abspath, "scons_local"))
try:
    from prereq_tools import PreReqComponent
except ImportError:
    raise ImportError \
          ("\'prereq_tools\' module not found; run \'git submodule update\'")

# Desired compiler flags that will be used if the compiler supports them.
DESIRED_FLAGS = ['-Wno-gnu-designator',
                 '-Wno-missing-braces',
                 '-Wno-gnu-zero-variadic-macro-arguments',
                 '-Wno-tautological-constant-out-of-range-compare']


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

    with open("cci.ini", "w") as ini_file:
        job_id = 1 + int(os.environ.get("BUILD_NUMBER", 0))
        port = 11000 + ((job_id * 50) % 6000)
        ini_file.write("""[server_tcp]
transport = tcp
interface = eth0
port = %d
priority = 10
mtu = 9000

[server_ib]
transport = verbs
interface = ib0
port = %d
priority = 10
""" % (port, port + 8000))
        ini_file.close()

    env.InstallAs('$PREFIX/etc/cci.ini', 'cci.ini')

def check_flag(context, flag):
    """Helper function to allow checking for compiler flags"""

    cc_name = context.env.get('CC')
    context.Message("Checking %s %s " % (cc_name, flag))
    context.env.Replace(CFLAGS=['-Werror', flag])
    ret = context.TryCompile("""
int main() {
    return 0;
}
""", ".c")
    context.Result(ret)
    return ret

def check_flag_cc(context, flag):
    """Helper function to allow checking for compiler flags"""

    cc_name = context.env.get('CXX')
    context.Message("Checking %s %s " % (cc_name, flag))
    context.env.Replace(CCFLAGS=['-Werror', flag])
    ret = context.TryCompile("""
int main() {
    return 0;
}
""", ".cpp")
    context.Result(ret)
    return ret

def run_checks(env, check_flags):
    """Run all configure time checks"""

    cenv = env.Clone()
    cenv.Append(CFLAGS='-Werror')
    config = Configure(cenv, custom_tests={'CheckFlag' : check_flag,
                                           'CheckFlagCC' : check_flag_cc})


    # Check for configure flags.
    # Some configure flags are always enabled (-g etc) however check in the
    # compiler supports other ones before using them.  Additional flags
    # might have been specified by the user in ~/.scons_localrc so check
    # those as well as DESIRED_FLAGS, and do it in such a way as user
    # flags come last so they can be used to disable locally defined flags.

    # Any flag that doesn't start with -W gets passed through unmodified.
    # pylint: disable=no-member

    checked = []
    for flag in check_flags:
        if flag in checked:
            continue
        if not flag.startswith('-W'):
            env.Append(CCFLAGS=[flag])
        elif config.CheckFlagCC(flag):
            env.Append(CCFLAGS=[flag])
        elif config.CheckFlag(flag):
            env.Append(CFLAGS=[flag])
        checked.append(flag)
    config.Finish()

    # pylint: enable=no-member

    print 'c Compiler options: %s %s %s' % (env.get('CC'),
                                            ' '.join(env.get('CFLAGS')),
                                            ' '.join(env.get('CCFLAGS')))
    print 'c++ Compiler options: %s %s' % (env.get('CXX'),
                                           ' '.join(env.get('CCFLAGS')))


def scons():
    """Scons function"""
    platform = os.uname()[0]
    opts_file = os.path.join(Dir('#').abspath, 'cart-%s.conf' % platform)

    commits_file = os.path.join(Dir('#').abspath, 'build.config')
    if not os.path.exists(commits_file):
        commits_file = None

    env = DefaultEnvironment()

    opts = Variables(opts_file)
    prereqs = PreReqComponent(env, opts,
                              config_file=commits_file, arch=platform)

    prereqs.preload(os.path.join(Dir('#').abspath,
                                 'scons_local',
                                 'components.py'),
                    prebuild=['ompi', 'mercury', 'uuid', 'crypto',
                              'pmix'])
    opts.Save(opts_file, env)
    env.Alias('install', '$PREFIX')

    if platform == 'Darwin':
        # generate .so on OSX instead of .dylib
        env.Append(SHLIBSUFFIX='.so')

    # Pull out the defined CFLAGS to use them later on for checking.
    check_flags = list(DESIRED_FLAGS)
    check_flags.extend(env.get('CFLAGS'))
    env.Replace(CFLAGS='')

    # Compiler options
    env.Append(CCFLAGS=['-g3', '-Wall', '-Werror', '-fpic', '-D_GNU_SOURCE'])
    env.Append(CCFLAGS=['-O2', '-pthread'])
    env.Append(CFLAGS=['-std=gnu99'])

    if not env.GetOption('clean'):
        run_checks(env, check_flags)

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
