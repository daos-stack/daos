"""DAOS functions for building go"""

import subprocess  # nosec B404
import os
import re
import json

from SCons.Script import Configure, GetOption, Scanner, Glob, Exit, File

GO_COMPILER = 'go'
MIN_GO_VERSION = '1.18.0'
include_re = re.compile(r'\#include [<"](\S+[>"])', re.M)


def _scan_go_file(node, env, _path):
    """Scanner for go code"""
    src_dir = os.path.dirname(str(node))
    includes = []
    path_name = str(node)[12:]
    rc = subprocess.run([env.d_go_bin, 'list', '--json', '-mod=vendor', path_name],
                        cwd='src/control', stdout=subprocess.PIPE, check=True)
    data = json.loads(rc.stdout.decode('utf-8'))
    for dep in data['Deps']:
        if not dep.startswith('github.com/daos-stack/daos'):
            continue

        deps_dir = dep[31:]
        includes.extend(Glob(f'src/{deps_dir}/*.go'))

    contents = node.get_contents()
    src_dir = os.path.dirname(str(node))
    for my_line in contents.splitlines():
        line = my_line.decode('utf-8')
        if line == 'import "C"':
            # With this line present in a go file all .c and .h files from the same directory will
            # be built so mark them all as dependencies.
            # https://go.p2hp.com/src/cmd/cgo/doc.go
            includes.extend(Glob(f'{src_dir}/*.c'))
            includes.extend(Glob(f'{src_dir}/*.h'))
        if line.startswith('#include'):
            new = include_re.findall(line)
            for dep in new:
                header = dep[:-1]
                if dep[-1] == '"':
                    includes.append(File(os.path.join(src_dir, header)))
                else:
                    includes.append(f'../../../include/{header}')

    return includes


def generate(env):
    """Setup the go compiler"""

    def _check_go_version(context):
        """Check GO Version"""
        context.Display('Checking for Go compiler... ')
        if env.d_go_bin:
            context.Display(env.d_go_bin + '\n')
        else:
            context.Result(0)
            return 0

        context.Display(f'Checking {env.d_go_bin} version... ')
        cmd_rc = subprocess.run([env.d_go_bin, 'version'], check=True, stdout=subprocess.PIPE)
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

    env.d_go_bin = env.get("GO_BIN", env.WhereIs(GO_COMPILER))

    if GetOption('help') or GetOption('clean'):
        return

    conf = Configure(env, custom_tests={'CheckGoVersion': _check_go_version})
    if not conf.CheckGoVersion():
        print('no usable Go compiler found (yum install golang?)')
        Exit(1)
    conf.Finish()

    # Must be run from the top of the source dir in order to pick up the vendored modules.
    # Propagate useful GO environment variables from the caller
    if 'GOCACHE' in os.environ:
        env['ENV']['GOCACHE'] = os.environ['GOCACHE']

    # Multiple go jobs can be running at once in scons via the -j option, there is no way to reserve
    # a number of scons job slots for a single command so if jobs is 1 then use that else use a
    # small number to allow progress without overloading the system.
    jobs = GetOption('num_jobs')
    if jobs == 1:
        env["ENV"]["GOMAXPROCS"] = '1'
    else:
        env["ENV"]["GOMAXPROCS"] = '5'

    env.Append(SCANNERS=Scanner(function=_scan_go_file, skeys=['.go']))


def exists(_env):
    """Tell SCons we exist"""
    return True
