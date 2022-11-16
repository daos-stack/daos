"""DAOS functions for building go"""

import subprocess
import os
import re

from SCons.Script import Configure, GetOption, Scanner, Glob, Exit

GO_COMPILER = 'go'
MIN_GO_VERSION = '1.16.0'
include_re = re.compile(r'\#include [<"](\S+)[>"]', re.M)
GOINC_PREFIX = '"github.com/daos-stack/daos/src/'


def _scan_go_file(node, _env, _path):
    """Scanner for go code"""
    contents = node.get_contents()
    src_dir = os.path.dirname(str(node))
    includes = []
    for my_line in contents.splitlines():
        line = my_line.decode('utf-8')
        if line.startswith('#include'):

            new = include_re.findall(line)
            for dep in new:
                if os.path.exists(os.path.join(src_dir, dep)):
                    includes.append(dep)
                else:
                    includes.append(f'../../../include/{dep}')
        elif line.strip().startswith(GOINC_PREFIX):
            # TODO: How does this work with src/control/lib where there's an extra level of
            # the tree?  Does go import each dir or is it implicit and therefore possibly
            # missed here.
            idir = line[len(GOINC_PREFIX) + 1:-1]
            deps_dir = os.path.join('src', idir)
            includes.extend(Glob(f'{deps_dir}/*.go'))

    return includes


def _setup_go(env):
    """Setup the go compiler"""

    def _check_go_version(context):
        """Check GO Version"""
        context.Display('Checking for Go compiler... ')
        if go_bin:
            context.Display(go_bin + '\n')
        else:
            context.Result(0)
            return 0

        context.Display(f'Checking {go_bin} version... ')
        cmd_rc = subprocess.run([go_bin, 'version'], check=True, stdout=subprocess.PIPE)
        out = cmd_rc.stdout.decode('utf-8')
        if len(out.split(' ')) < 3:
            context.Result(f'failed to get version from "{out}"')
            return 0

        # go version go1.2.3 Linux/amd64
        go_version = out.split(' ')[2].replace('go', '')
        if len([x for x, y in
                zip(go_version.split('.'), MIN_GO_VERSION.split('.'))
                if int(x) < int(y)]) > 0:
            context.Result(f'{go_version} is too old (min supported: {MIN_GO_VERSION}) ')
            return 0
        context.Result(str(go_version))
        return 1

    if GetOption('help') or GetOption('clean'):
        return

    go_bin = env.get("GO_BIN", env.WhereIs(GO_COMPILER))

    conf = Configure(env, custom_tests={'CheckGoVersion': _check_go_version})
    if not conf.CheckGoVersion():
        print('no usable Go compiler found (yum install golang?)')
        Exit(1)
    conf.Finish()
    env.d_go_bin = go_bin
    env.Append(SCANNERS=Scanner(function=_scan_go_file, skeys=['.go']))


def setup(env):
    """Add daos specific methods to environment"""
    env.AddMethod(_setup_go, 'd_setup_go')
