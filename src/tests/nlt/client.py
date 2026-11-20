"""NLT: daos client command helpers, pools and containers.

(C) Copyright 2020-2024 Intel Corporation.
(C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP
(C) Copyright 2025 Google LLC
(C) Copyright 2025 Enakta Labs Ltd

SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import json
import os
import pprint
import re
import subprocess  # nosec
import tempfile
from os.path import join
from types import NoneType

from .base import get_inc_id
from .config import get_base_env
from .logging_utils import log_test


class DaosPool():
    """Class to store data about daos pools"""

    def __init__(self, server, pool_uuid, label):
        self._server = server
        self.uuid = pool_uuid
        self.label = label
        self.conf = server.conf

    # pylint: disable-next=invalid-name
    def id(self):
        """Return the pool ID (label if set; UUID otherwise)"""
        if self.label:
            return self.label
        return self.uuid

    def dfuse_mount_name(self):
        """Return the string to pass to dfuse mount

        This should be a label if set, otherwise just the
        uuid.
        """
        return self.id()

    def fetch_containers(self):
        """Query the server and return a list of container objects"""
        rc = run_daos_cmd(self._server.conf, ['container', 'list', self.uuid], use_json=True)

        data = rc.json

        assert data['status'] == 0, rc
        assert data['error'] is None, rc

        if data['response'] is None:
            print('No containers in pool')
            return []

        containers = []
        for cont in data['response']:
            containers.append(DaosCont(cont['uuid'], cont['label'], pool=self))
        return containers


class DaosCont():
    """Class to store data about daos containers"""

    def __init__(self, cont_uuid, label, pool):
        if not isinstance(pool, (DaosPool, NoneType)):
            raise ValueError('pool must be DaosPool or None')
        self.uuid = cont_uuid
        self.label = label
        self.pool = pool

    # pylint: disable-next=invalid-name
    def id(self):
        """Return the container ID (label if set; UUID otherwise)"""
        if self.label:
            return self.label
        return self.uuid

    def set_attrs(self, attrs):
        """Set container attributes.

        Args:
            attrs (dict): Dictionary of attributes to set.
        """
        kvs = []
        for key, value in attrs.items():
            kvs.append(f'{key}:{value}')

        cmd = ['container', 'set-attr', self.pool.id(), self.id(), ','.join(kvs)]

        rc = run_daos_cmd(self.pool.conf, cmd, show_stdout=True)
        print(rc)
        assert rc.returncode == 0, rc

    def destroy(self, valgrind=True, log_check=True, force=False):
        """Destroy the container

        Args:
            valgrind (bool, optional): Run the command under valgrind. Defaults to True.
            log_check (bool, optional): Run log analysis. Defaults to True.

        Raises:
            NLTestFail: If Pool was not provided when object created.
        """
        destroy_container(self.pool.conf, self.pool.id(), self.id(),
                          valgrind=valgrind, log_check=log_check, force=force)


class ValgrindHelper():
    """Class for running valgrind commands

    This helps setup the command line required, and
    performs log modification after the fact to assist
    Jenkins in locating the source code.
    """

    def __init__(self, conf, logid=None):

        # Set this to False to disable valgrind, which will run faster.
        self.conf = conf
        self.use_valgrind = True
        self.full_check = True
        self._xml_file = None
        self._logid = logid

        # Repo root, three levels above this package (src/tests/nlt/ -> src/tests/ -> src/ -> root).
        src_dir = os.path.realpath(
            os.path.dirname(os.path.dirname(os.path.dirname(
                os.path.dirname(os.path.abspath(__file__))))))
        self.src_dir = f'{src_dir}/'

    def get_cmd_prefix(self):
        """Return the command line prefix"""
        if not self.use_valgrind:
            return []

        if not self._logid:
            self._logid = get_inc_id()

        with tempfile.NamedTemporaryFile(prefix=f'dnt.{self._logid}.', dir='.',
                                         suffix='.memcheck', delete=False) as log_file:
            self._xml_file = log_file.name

        cmd = ['valgrind',
               f'--xml-file={self._xml_file}',
               '--xml=yes',
               '--fair-sched=yes',
               '--gen-suppressions=all',
               '--error-exitcode=42']

        if self.conf.args.valgrind_verbose:
            cmd.append('--verbose')

        if self.full_check:
            cmd.extend(['--leak-check=full', '--show-leak-kinds=all'])
        else:
            cmd.append('--leak-check=no')

        src_suppression_file = join('src', 'cart', 'utils', 'memcheck-cart.supp')
        if os.path.exists(src_suppression_file):
            cmd.append(f'--suppressions={src_suppression_file}')
        else:
            cmd.append(f"--suppressions={join(self.conf['PREFIX'], 'etc', 'memcheck-cart.supp')}")

        return cmd

    def add_memcheck_env(self, env):
        """Adjust the Go runtime for a command run under memcheck."""
        if not self.use_valgrind:
            return
        godebug = env.get('GODEBUG')
        env['GODEBUG'] = f'{godebug},asyncpreemptoff=1' if godebug else 'asyncpreemptoff=1'
        # disable GC, as it wastes time and interacts poorly with valgrind
        env['GOGC'] = 'off'

    def convert_xml(self):
        """Modify the xml file"""
        if not self.use_valgrind:
            return
        with open(self._xml_file, 'r') as fd:
            with open(f'{self._xml_file}.xml', 'w') as ofd:
                for line in fd:
                    ofd.write(line.replace(self.src_dir, ''))
        os.unlink(self._xml_file)


def assert_file_size_fd(fd, size):
    """Verify the file size is as expected"""
    my_stat = os.fstat(fd)
    print(f'Checking file size is {size} {my_stat.st_size}')
    assert my_stat.st_size == size


def assert_file_size(ofd, size):
    """Verify the file size is as expected"""
    assert_file_size_fd(ofd.fileno(), size)


def import_daos(server):
    """Return a handle to the pydaos module"""

    os.environ['DD_MASK'] = 'all'
    os.environ['DD_SUBSYS'] = 'all'
    os.environ['D_LOG_MASK'] = 'DEBUG'
    os.environ['FI_UNIVERSE_SIZE'] = '128'
    os.environ['DAOS_AGENT_DRPC_DIR'] = server.agent_dir

    daos = __import__('pydaos')
    return daos


class DaosCmdReturn():
    """Class to enable pretty printing of daos output"""

    def __init__(self):
        self.rc = None
        self.valgrind = []
        self.cmd = []

    def __getattr__(self, item):
        return getattr(self.rc, item)

    def __str__(self):
        if not self.rc:
            return 'daos_command_return, process not yet run'
        command = ' '.join(self.cmd)
        output = f"CompletedDaosCommand(cmd='{command}')"
        output += f'\nReturncode is {self.rc.returncode}'
        if self.valgrind:
            command = ' '.join(self.valgrind)
            output += f"\nProcess ran under valgrind with '{command}'"
        try:
            output += '\njson output:\n' + pprint.PrettyPrinter().pformat(self.rc.json)
        except AttributeError:
            for line in self.rc.stdout.splitlines():
                output += f'\nstdout: {line}'

        for line in self.rc.stderr.splitlines():
            output += f'\nstderr: {line}'
        return output


def run_daos_cmd(conf,
                 cmd,
                 show_stdout=False,
                 valgrind=True,
                 log_check=True,
                 ignore_busy=False,
                 use_json=False,
                 cwd=None):
    """Run a DAOS command

    Run a command, returning what subprocess.run() would.

    Enable logging, and valgrind for the command.
    """
    dcr = DaosCmdReturn()
    valgrind_hdl = ValgrindHelper(conf)

    if conf.args.memcheck == 'no':
        valgrind = False

    if not valgrind:
        valgrind_hdl.use_valgrind = False

    exec_cmd = valgrind_hdl.get_cmd_prefix()
    dcr.valgrind = list(exec_cmd)
    daos_cmd = [join(conf['PREFIX'], 'bin', 'daos')]
    if use_json:
        daos_cmd.append('--json')
    daos_cmd.extend(cmd)
    dcr.cmd = daos_cmd
    exec_cmd.extend(daos_cmd)

    cmd_env = get_base_env()
    valgrind_hdl.add_memcheck_env(cmd_env)

    if conf.args.client_debug:
        cmd_env['D_LOG_MASK'] = conf.args.client_debug

    if not log_check:
        del cmd_env['DD_MASK']
        del cmd_env['DD_SUBSYS']
        del cmd_env['D_LOG_MASK']

    with tempfile.NamedTemporaryFile(prefix=f'dnt_cmd_{get_inc_id()}_',
                                     suffix='.log',
                                     dir=conf.tmp_dir,
                                     delete=False) as log_file:
        log_name = log_file.name
        cmd_env['D_LOG_FILE'] = log_name
        with open(log_file.name, 'w', encoding='utf-8') as lf:
            lf.write(f'cmd: {" ".join(cmd)}\n')

    cmd_env['DAOS_AGENT_DRPC_DIR'] = conf.agent_dir

    rc = subprocess.run(exec_cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                        env=cmd_env, check=False, cwd=cwd)

    if rc.stderr != b'':
        print('Stderr from command')
        print(rc.stderr.decode('utf-8').strip())

    if show_stdout and rc.stdout != b'':
        print(rc.stdout.decode('utf-8').strip())

    show_memleaks = True

    # A negative return code means the process exited with a signal so do not
    # check for memory leaks in this case as it adds noise, right when it's
    # least wanted.
    if rc.returncode < 0:
        show_memleaks = False

    rc.fi_loc = log_test(conf, log_name, show_memleaks=show_memleaks, ignore_busy=ignore_busy)
    valgrind_hdl.convert_xml()
    # If there are valgrind errors here then mark them for later reporting but
    # do not abort.  This allows a full-test run to report all valgrind issues
    # in a single test run.
    if valgrind_hdl.use_valgrind and rc.returncode == 42:
        print("Valgrind errors detected")
        print(rc)
        conf.wf.add_test_case(' '.join(cmd), failure='valgrind errors', output=rc)
        conf.valgrind_errors = True
        rc.returncode = 0
    if use_json:
        rc.json = json.loads(rc.stdout.decode('utf-8'))
    dcr.rc = rc
    return dcr


# pylint: disable-next=too-many-arguments
def create_cont(conf, pool=None, ctype=None, label=None, path=None, oclass=None, dir_oclass=None,
                file_oclass=None, hints=None, valgrind=False, log_check=True, cwd=None, attrs=None):
    """Use 'daos' command to create a new container.

    Args:
        conf (NLTConf): NLT configuration object.
        pool (DaosPool or str, optional): Pool to create container in.
              Not required when path is set.
        ctype (str, optional): Container type.
        label (str, optional): Container label.
        path (str, optional): Path to use when creating container.
        oclass (str, optional): object class to use.
        dir_oclass (str, optional): directory object class to use.
        file_oclass (str, optional): file object class to use.
        hints (str, optional): Container hints.
        valgrind (bool, optional): Whether to run command under valgrind.  Defaults to True.
        log_check (bool, optional): Whether to run log analysis to check for leaks.
        cwd (str, optional): Path to run daos command from.
        attrs (dict, optional): Dictionary of user attributes to set.

    Returns:
        DaosCont: Newly created container as DaosCont object.

    Raises:
        ValueError: on invalid inputs
    """
    cmd = ['container', 'create']

    if not isinstance(pool, (DaosPool, NoneType)):
        raise ValueError('pool must be DaosPool or None')

    if not path and not pool:
        raise ValueError('pool or path is required')

    if pool:
        cmd.append(pool.id())

    if label:
        cmd.append(label)

    if path:
        cmd.extend(['--path', path])
        ctype = 'POSIX'

    if ctype:
        cmd.extend(['--type', ctype])

    if oclass:
        cmd.extend(['--oclass', oclass])

    if dir_oclass:
        cmd.extend(['--dir-oclass', dir_oclass])

    if file_oclass:
        cmd.extend(['--file-oclass', file_oclass])

    if hints:
        cmd.extend(['--hints', hints])

    if attrs:
        cmd.extend(['--attrs', ','.join([f"{name}:{val}" for name, val in attrs.items()])])

    cmd.extend(['--properties', 'cksum:off,srv_cksum:off,rd_fac:0'])

    def _create_cont():
        """Helper function for create_cont"""
        rc = run_daos_cmd(conf, cmd, use_json=True, log_check=log_check, valgrind=valgrind,
                          cwd=cwd)
        print(rc)
        return rc

    rc = _create_cont()

    if rc.returncode == 1 and \
       rc.json['error'] == 'failed to create container: DER_EXIST(-1004): Entity already exists':

        # If a path is set DER_EXIST may refer to the path, not a container so do not attempt to
        # remove and retry in this case.
        if path is None:
            destroy_container(conf, pool, label)
            rc = _create_cont()

    assert rc.returncode == 0, rc
    if label:
        assert label == rc.json['response']['container_label']
    else:
        assert 'container_label' not in rc.json['response'].keys()
    return DaosCont(rc.json['response']['container_uuid'], label, pool=pool)


def destroy_container(conf, pool, container, valgrind=True, log_check=True, force=False):
    """Destroy a container"""
    if isinstance(pool, DaosPool):
        pool = pool.id()
    if isinstance(container, DaosCont):
        container = container.id()
    cmd = ['container', 'destroy', pool, container]
    if force:
        cmd.append("--force")
    rc = run_daos_cmd(conf, cmd, valgrind=valgrind, use_json=True, log_check=log_check)
    print(rc)
    if rc.returncode == 1 and rc.json['status'] == -1012:
        # This shouldn't happen but can on unclean shutdown, file it as a test failure so it does
        # not get lost, however destroy the container and attempt to continue.
        # DAOS-8860
        conf.wf.add_test_case(f'destroy_container_{pool}/{container}',
                              failure='Failed to destroy container',
                              output=rc)
        cmd = ['container', 'destroy', '--force', pool, container]
        rc = run_daos_cmd(conf, cmd, valgrind=valgrind, use_json=True)
        print(rc)
    assert rc.returncode == 0, rc


def run_fs_get_attr(conf, *args):
    """Get json data from daos fs get-attr"""
    cmd = ['fs', 'get-attr']
    cmd.extend(args)
    print(f"run daos {' '.join(cmd)}")
    rc = run_daos_cmd(conf, cmd, show_stdout=True, use_json=True)

    data = rc.json
    assert data['status'] == 0, rc
    assert data['error'] is None, rc
    assert data['response'] is not None, rc

    return data


def check_fs_get_attr_oid(data_in, check_type):
    """Verify a valid oid in output"""
    if check_type not in data_in:
        print(f"{check_type} not found")
        return False
    data = data_in[check_type]
    if 'oid' not in data:
        print("oid is not found!!!")
        return False
    oid = data['oid']
    if not re.match(r"^\d+\.\d+$", oid):
        print(f"oid does not match expected pattern {oid}")
        return False
    return True


def check_fs_get_attr(data_in, check_type, **checks):
    """Verify daos fs tool output"""
    if check_type not in data_in:
        print(f"{check_type} not found")
        return False
    data = data_in[check_type]
    for key, value in checks.items():
        if value is None:
            continue
        if key not in data:
            print(f"expected json attribute {key} not found")
            return False
        if data[key] != value:
            print(f"expected json attribute {key} has unexpected value {value} "
                  f"!= {data[key]}")
            return False
    return True


def check_file_attr(data, oclass, csize):
    """Verify daos fs tool output"""
    if not check_fs_get_attr_oid(data, 'response'):
        return False
    return check_fs_get_attr(data, 'response', oclass=oclass, chunk_size=csize)


def check_dir_attr(data, oclass, file_oclass, dir_oclass, csize):
    """Verify daos fs tool output"""
    if not check_fs_get_attr_oid(data['response'], 'object'):
        return False
    if not check_fs_get_attr(data['response'], 'object', oclass=oclass):
        return False

    return check_fs_get_attr(data['response'], 'directory', dir_oclass=dir_oclass,
                             file_oclass=file_oclass, chunk_size=csize)
