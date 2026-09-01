"""NLT: single-node DAOS server management.

(C) Copyright 2020-2024 Intel Corporation.
(C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP
(C) Copyright 2025 Google LLC
(C) Copyright 2025 Enakta Labs Ltd

SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import copy
import json
import os
import re
import signal
import stat
import subprocess  # nosec
import sys
import tempfile
import time
from os.path import join

import yaml

from .base import NLTestFail, NLTestTimeout, get_inc_id
from .client import DaosCont, DaosPool, ValgrindHelper
from .config import get_base_env
from .logging_utils import log_test


class DaosServer():
    """Manage a DAOS server instance"""

    def __init__(self, conf, test_class=None, valgrind=False, wf=None, fatal_errors=None,
                 enable_fi=False, wipe_on_exit=False):
        self.running = False
        self._file = __file__.lstrip('./')
        self._sp = None
        self._fi = enable_fi
        self._fi_file = None
        self.wf = wf
        self.fatal_errors = fatal_errors
        self.conf = conf
        if test_class:
            self._test_class = f'Server.{test_class}'
        else:
            self._test_class = None
        self.valgrind = valgrind
        self._agent = None
        self.max_start_time = 120
        self.max_stop_time = 30
        self.stop_sleep_time = 0.5
        self.engines = conf.args.engine_count
        self.sys_ram_rsvd = conf.args.system_ram_reserved
        # pylint: disable=consider-using-with
        self.control_log = tempfile.NamedTemporaryFile(prefix='dnt_control_',
                                                       suffix='.log',
                                                       dir=conf.tmp_dir,
                                                       delete=False)
        self.helper_log = tempfile.NamedTemporaryFile(prefix='dnt_helper_',
                                                      suffix='.log',
                                                      dir=conf.tmp_dir,
                                                      delete=False)
        self.agent_log = tempfile.NamedTemporaryFile(prefix='dnt_agent_',
                                                     suffix='.log',
                                                     dir=conf.tmp_dir,
                                                     delete=False)
        self.server_logs = []
        for engine in range(self.engines):
            prefix = f'dnt_server_{self._test_class}_{engine}_'
            self.server_logs.append(tempfile.NamedTemporaryFile(prefix=prefix,
                                                                suffix='.log',
                                                                dir=conf.tmp_dir,
                                                                delete=False))
        self.__process_name = 'daos_engine'
        if self.valgrind:
            self.__process_name = 'memcheck-amd64-'

        os.makedirs('/tmp/dnt_sockets', exist_ok=True)

        self.agent_dir = tempfile.mkdtemp(prefix='dnt_agent_')

        self._yaml_file = None
        self._io_server_dir = None
        self.test_pool = None
        self.network_interface = None
        self.network_provider = None

        self.fuse_procs = []
        self.wipe_on_exit = wipe_on_exit
        self.scm_mounts = []

    def __enter__(self):
        self._start()
        return self

    def __exit__(self, _type, _value, _traceback):
        rc = self._stop(self.wf)
        if rc != 0 and self.fatal_errors is not None:
            self.fatal_errors.fail()
        if self.wipe_on_exit:
            for mount in self.scm_mounts:
                ret = subprocess.run(['sudo', 'umount', mount], check=False)
                print(f'rc from umount {mount}: {ret.returncode}')
        return False

    def add_fuse(self, fuse):
        """Register a new fuse instance"""
        self.fuse_procs.append(fuse)

    def remove_fuse(self, fuse):
        """Deregister a fuse instance"""
        self.fuse_procs.remove(fuse)

    def __del__(self):
        if self._agent:
            self._stop_agent()
        try:
            if self.running:
                self._stop(None)
        except NLTestTimeout:
            print('Ignoring timeout on stop')
        server_file = join(self.agent_dir, '.daos_server.active.yml')
        if os.path.exists(server_file):
            os.unlink(server_file)
        for log in self.server_logs:
            if os.path.exists(log.name):
                log_test(self.conf, log.name)
        try:
            os.unlink(join(self.agent_dir, 'nlt_agent.yaml'))
            os.rmdir(self.agent_dir)
        except OSError as error:
            print(os.listdir(self.agent_dir))
            raise error

    def _add_test_case(self, name, failure=None, duration=None):
        """Add a test case to the server instance

        Simply wrapper to automatically add the class
        """
        if not self._test_class:
            return

        self.conf.wf.add_test_case(name, failure=failure, duration=duration,
                                   test_class=self._test_class)

    def _check_timing(self, name, start, max_time):
        elapsed = time.perf_counter() - start
        if elapsed > max_time:
            res = f'{name} failed after {elapsed:.2f}s (max {max_time:.2f}s)'
            self._add_test_case(name, duration=elapsed, failure=res)
            raise NLTestTimeout(res)

    def _check_system_state(self, desired_states):
        """Check the system state for against list

        Return true if all members are in a state specified by the
        desired_states.
        """
        if not isinstance(desired_states, list):
            desired_states = [desired_states]

        rc = self.run_dmg(['system', 'query', '--json'])
        if rc.returncode != 0:
            return False
        data = json.loads(rc.stdout.decode('utf-8'))
        if data['error'] or data['status'] != 0:
            return False
        members = data['response']['members']
        if members is None:
            return False
        if len(members) != self.engines:
            return False

        for member in members:
            if member['state'] not in desired_states:
                return False
        return True

    def _start(self):
        """Start a DAOS server"""
        # pylint: disable=consider-using-with
        server_env = get_base_env(clean=True)

        plain_env = os.environ.copy()

        if self.valgrind:
            valgrind_args = ['--fair-sched=yes',
                             '--gen-suppressions=all',
                             '--xml=yes',
                             '--xml-file=dnt.server.%p.memcheck.xml',
                             '--num-callers=10',
                             '--track-origins=yes',
                             '--leak-check=full']
            suppression_file = join('src', 'cart', 'utils', 'memcheck-cart.supp')
            if not os.path.exists(suppression_file):
                suppression_file = join(self.conf['PREFIX'], 'etc', 'memcheck-cart.supp')
            valgrind_args.append(f'--suppressions={os.path.realpath(suppression_file)}')

            go_suppression_file = join('src', 'cart', 'utils', 'memcheck-go.supp')
            if not os.path.exists(go_suppression_file):
                go_suppression_file = join(self.conf['PREFIX'], 'etc', 'memcheck-go.supp')
            valgrind_args.append(f'--suppressions={os.path.realpath(go_suppression_file)}')

            self._io_server_dir = tempfile.TemporaryDirectory(prefix='dnt_io_')

            with open(join(self._io_server_dir.name, 'daos_engine'), 'w') as fd:
                fd.write('#!/bin/sh\n')
                fd.write(f"export PATH={join(self.conf['PREFIX'], 'bin')}:$PATH\n")
                fd.write(f'exec valgrind {" ".join(valgrind_args)} daos_engine "$@"\n')

            os.chmod(join(self._io_server_dir.name, 'daos_engine'),
                     stat.S_IXUSR | stat.S_IRUSR)

            plain_env['PATH'] = f'{self._io_server_dir.name}:{plain_env["PATH"]}'
            self.max_start_time = 300
            self.max_stop_time = 600
            self.stop_sleep_time = 10

        daos_server = join(self.conf['PREFIX'], 'bin', 'daos_server')

        # nlt_server.yaml ships alongside this package.
        self_dir = os.path.dirname(os.path.abspath(__file__))

        # Create a server yaml file.  To do this open and copy the
        # nlt_server.yaml file in the current directory, but overwrite
        # the server log file with a temporary file so that multiple
        # server runs do not overwrite each other.
        with open(join(self_dir, 'nlt_server.yaml'), 'r') as scfd:
            scyaml = yaml.safe_load(scfd)
        if self.conf.args.server_debug:
            scyaml['engines'][0]['log_mask'] = self.conf.args.server_debug
        scyaml['control_log_file'] = self.control_log.name
        scyaml['helper_log_file'] = self.helper_log.name

        scyaml['socket_dir'] = self.agent_dir

        if self._fi:
            # Set D_ALLOC to fail, but do not enable it.  This can be changed later via
            # the set_fi() method.
            faults = {'fault_config': [{'id': 0,
                                        'probability_x': 0,
                                        'probability_y': 100}]}

            self._fi_file = tempfile.NamedTemporaryFile(prefix='fi_', suffix='.yaml')

            self._fi_file.write(yaml.dump(faults, encoding='utf=8'))
            self._fi_file.flush()
            server_env['D_FI_CONFIG'] = self._fi_file.name

        for (key, value) in server_env.items():
            # If server log is set via server_debug then do not also set env settings.
            if self.conf.args.server_debug and key in ('DD_MASK', 'DD_SUBSYS', 'D_LOG_MASK'):
                continue
            scyaml['engines'][0]['env_vars'].append(f'{key}={value}')

        if self.sys_ram_rsvd is not None:
            scyaml['system_ram_reserved'] = self.sys_ram_rsvd

        ref_engine = copy.deepcopy(scyaml['engines'][0])
        scyaml['engines'] = []
        server_port_count = int(server_env['FI_UNIVERSE_SIZE'])
        self.network_interface = ref_engine['fabric_iface']
        self.network_provider = scyaml['provider']
        for idx in range(self.engines):
            engine = copy.deepcopy(ref_engine)
            engine['log_file'] = self.server_logs[idx].name
            engine['first_core'] = ref_engine['targets'] * idx
            engine['fabric_iface_port'] += server_port_count * idx
            engine['storage'][0]['scm_mount'] = f'{ref_engine["storage"][0]["scm_mount"]}_{idx}'
            self.scm_mounts.append(engine['storage'][0]['scm_mount'])
            scyaml['engines'].append(engine)
        self._yaml_file = tempfile.NamedTemporaryFile(prefix='nlt-server-config-', suffix='.yaml')
        self._yaml_file.write(yaml.dump(scyaml, encoding='utf-8'))
        self._yaml_file.flush()

        cmd = [daos_server, 'start', f'--config={self._yaml_file.name}', '--insecure']

        # pylint: disable=consider-using-with
        self._sp = subprocess.Popen(cmd, env=plain_env)

        agent_config = join(self.agent_dir, 'nlt_agent.yaml')
        with open(agent_config, 'w') as fd:
            agent_data = {
                'access_points': scyaml['mgmt_svc_replicas'],
                'control_log_mask': 'NOTICE',  # INFO logs every client process connection
            }
            json.dump(agent_data, fd)

        agent_bin = join(self.conf['PREFIX'], 'bin', 'daos_agent')

        agent_cmd = [agent_bin,
                     '--config-path', agent_config,
                     '--insecure',
                     '--runtime_dir', self.agent_dir,
                     '--logfile', self.agent_log.name]

        if not self.conf.args.server_debug and not self.conf.args.client_debug:
            agent_cmd.append('--debug')

        self._agent = subprocess.Popen(agent_cmd)
        self.conf.agent_dir = self.agent_dir

        # Configure the storage.  DAOS wants to mount /mnt/daos itself if not
        # already mounted, so let it do that.
        # This code supports three modes of operation:
        # /mnt/daos is not mounted.  It will be mounted and formatted.
        # /mnt/daos exists and has data in.  It will be used as is.
        # /mnt/daos is mounted but empty.  It will be used-as is.
        # In this last case the --no-root option must be used.
        start = time.perf_counter()

        cmd = ['storage', 'format', '--json']
        start_timeout = 0.5
        while True:
            try:
                rc = self._sp.wait(timeout=start_timeout)
                print(rc)
                res = 'daos server died waiting for start'
                self._add_test_case('format', failure=res)
                raise NLTestFail(res)
            except subprocess.TimeoutExpired:
                pass
            rc = self.run_dmg(cmd)

            data = json.loads(rc.stdout.decode('utf-8'))
            print(f'cmd: {cmd} data: {data}')

            if data['error'] is None:
                break

            if 'running system' in data['error']:
                break

            if start_timeout < 5:
                start_timeout *= 2

            self._check_timing('format', start, self.max_start_time)
        duration = time.perf_counter() - start
        self._add_test_case('format', duration=duration)
        print(f'Format completion in {duration:.2f} seconds')
        self.running = True

        # Now wait until the system is up, basically the format to happen.
        start_timeout = 0.5
        while True:
            time.sleep(start_timeout)
            if self._check_system_state(['ready', 'joined']):
                break

            if start_timeout < 5:
                start_timeout *= 2

            self._check_timing("start", start, self.max_start_time)
        duration = time.perf_counter() - start
        self._add_test_case('start', duration=duration)
        print(f'Server started in {duration:.2f} seconds')
        self.fetch_pools()

    def _stop_agent(self):
        self._agent.send_signal(signal.SIGINT)
        ret = self._agent.wait(timeout=5)
        print(f'rc from agent is {ret}')
        self._agent = None
        try:
            os.unlink(join(self.agent_dir, 'daos_agent.sock'))
        except FileNotFoundError:
            pass

    def _stop(self, wf):
        """Stop a previously started DAOS server"""
        for fuse in self.fuse_procs:
            print('Stopping server with running fuse procs, cleaning up')
            self._add_test_case('server-stop-with-running-fuse', failure=str(fuse))
            fuse.stop()

        if self._agent:
            self._stop_agent()

        if not self._sp:
            return 0

        # Check the correct number of processes are still running at this
        # point, in case anything has crashed.  daos_server does not
        # propagate errors, so check this here.
        parent_pid = self._sp.pid
        procs = []
        for proc_id in os.listdir('/proc/'):
            if proc_id == 'self':
                continue
            status_file = f'/proc/{proc_id}/status'
            if not os.path.exists(status_file):
                continue
            with open(status_file, 'r') as fd:
                for line in fd.readlines():
                    try:
                        key, raw = line.split(':', maxsplit=1)
                    except ValueError:
                        continue
                    value = raw.strip()
                    if key == 'Name' and value != self.__process_name:
                        break
                    if key != 'PPid':
                        continue
                    if int(value) == parent_pid:
                        procs.append(proc_id)
                        break

        if len(procs) != self.engines:
            # Mark this as a warning, but not a failure.  This is currently
            # expected when running with preexisting data because the server
            # is calling exec.  Do not mark as a test failure for the same
            # reason.
            entry = {}
            entry['fileName'] = self._file
            # pylint: disable=protected-access
            entry['lineStart'] = sys._getframe().f_lineno
            entry['severity'] = 'NORMAL'
            message = f'Incorrect number of engines running ({len(procs)} vs {self.engines})'
            entry['message'] = message
            self.conf.wf.issues.append(entry)
            self._add_test_case('server_stop', failure=message)
        start = time.perf_counter()
        rc = self.run_dmg(['system', 'stop', '--full'])
        if rc.returncode != 0:
            print(rc)
            entry = {}
            entry['fileName'] = self._file
            # pylint: disable=protected-access
            entry['lineStart'] = sys._getframe().f_lineno
            entry['severity'] = 'ERROR'
            msg = f'dmg system stop --full failed with {rc.returncode}'
            entry['message'] = msg
            self.conf.wf.issues.append(entry)
        if not self.valgrind:
            assert rc.returncode == 0, rc
        while True:
            time.sleep(self.stop_sleep_time)
            if self._check_system_state(['stopped', 'errored']):
                break
            self._check_timing("stop", start, self.max_stop_time)

        duration = time.perf_counter() - start
        self._add_test_case('stop', duration=duration)
        print(f'Server stopped in {duration:.2f} seconds')

        self._sp.send_signal(signal.SIGTERM)
        ret = self._sp.wait(timeout=5)
        print(f'rc from server is {ret}')

        self.conf.compress_file(self.agent_log.name)
        self.conf.compress_file(self.control_log.name)
        self.conf.compress_file(self.helper_log.name)

        # Always keep the shared server logs; analyze the engine log for findings but do not let
        # log_test prune it.  Only the client logs (per-command/dfuse/FI) are pruned when clean.
        for log in self.server_logs:
            log_test(self.conf, log.name, leak_wf=wf, skip_fi=self._fi, defer_prune=True)
            if os.path.exists(log.name):
                self.conf.compress_file(log.name)
        self.server_logs = []
        self.running = False
        return ret

    def run_dmg(self, cmd):
        """Run the specified dmg command"""
        exe_cmd = [join(self.conf['PREFIX'], 'bin', 'dmg')]
        exe_cmd.append('--insecure')
        exe_cmd.extend(cmd)

        print(f'running {exe_cmd}')
        return subprocess.run(exe_cmd,
                              stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE,
                              check=False)

    def run_dmg_json(self, cmd):
        """Run the specified dmg command in json mode

        return data as json, or raise exception on failure
        """
        cmd.append('--json')
        rc = self.run_dmg(cmd)
        print(rc)
        assert rc.returncode == 0
        assert rc.stderr == b''
        data = json.loads(rc.stdout.decode('utf-8'))
        assert not data['error']
        assert data['status'] == 0
        assert data['response']['status'] == 0
        return data

    def fetch_pools(self):
        """Query the server and return a list of pool objects"""
        data = self.run_dmg_json(['pool', 'list'])

        # This should exist but might be 'None' so check for that rather than
        # iterating.
        pools = []
        if not data['response']['pools']:
            return pools
        for pool in data['response']['pools']:
            pobj = DaosPool(self, pool['uuid'], pool.get('label', None))
            pools.append(pobj)
            if pobj.label == 'NLT':
                self.test_pool = pobj
        return pools

    def _make_pool(self):
        """Create a DAOS pool"""
        # If running as a small system with tmpfs already mounted then this is likely a docker
        # container so restricted in size.
        if self.conf.args.no_root:
            size = 1024 * 2
        else:
            size = 1024 * 4

        rc = self.run_dmg(['pool', 'create', 'NLT', '--scm-size', f'{size}M', '--properties',
                           'rd_fac:0,space_rb:0'])
        print(rc)
        assert rc.returncode == 0
        self.fetch_pools()

    def get_test_pool_obj(self):
        """Return a pool object to be used for testing

        Create a pool as required
        """
        if self.test_pool is None:
            self._make_pool()

        return self.test_pool

    def run_daos_client_cmd(self, cmd):
        """Run a DAOS client

        Run a command, returning what subprocess.run() would.

        Enable logging, and valgrind for the command.
        """
        valgrind_hdl = ValgrindHelper(self.conf)

        if self.conf.args.memcheck == 'no':
            valgrind_hdl.use_valgrind = False

        exec_cmd = valgrind_hdl.get_cmd_prefix()

        exec_cmd.extend(cmd)

        cmd_env = get_base_env()
        valgrind_hdl.add_memcheck_env(cmd_env)

        with tempfile.NamedTemporaryFile(prefix=f'dnt_cmd_{get_inc_id()}_',
                                         suffix='.log',
                                         dir=self.conf.tmp_dir,
                                         delete=False) as log_file:
            log_name = log_file.name
            cmd_env['D_LOG_FILE'] = log_name
            with open(log_name, 'w', encoding='utf-8') as lf:
                lf.write(f'cmd: {" ".join(cmd)}\n')

        cmd_env['DAOS_AGENT_DRPC_DIR'] = self.conf.agent_dir

        rc = subprocess.run(exec_cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            env=cmd_env, check=False)

        if rc.stderr != b'':
            print('Stderr from command')
            print(rc.stderr.decode('utf-8').strip())

        if rc.stdout != b'':
            print(rc.stdout.decode('utf-8').strip())

        show_memleaks = True

        # A negative return code means the process exited with a signal so do not
        # check for memory leaks in this case as it adds noise, right when it's
        # least wanted.
        if rc.returncode < 0:
            show_memleaks = False

        rc.fi_loc = log_test(self.conf, log_name, show_memleaks=show_memleaks)
        valgrind_hdl.convert_xml()
        # If there are valgrind errors here then mark them for later reporting but
        # do not abort.  This allows a full-test run to report all valgrind issues
        # in a single test run.
        if valgrind_hdl.use_valgrind and rc.returncode == 42:
            print("Valgrind errors detected")
            print(rc)
            self.conf.wf.add_test_case(' '.join(cmd), failure='valgrind errors', output=rc)
            self.conf.valgrind_errors = True
            rc.returncode = 0
        assert rc.returncode == 0, rc

    def run_daos_client_cmd_pil4dfs(self, cmd, check=True, container=None, report=True):
        """Run a DAOS client with libpil4dfs.so

        Run a command, returning what subprocess.run() would.

        If container is supplied setup the environment to access that container, using a temporary
        directory as a "mount point" and run the command from that directory so that paths can be
        relative.

        Looks like valgrind and libpil4dfs.so do not work together sometime. Disable valgrind at
        this moment. Will revisit this issue later.
        """
        if container is not None:
            assert isinstance(container, DaosCont)

        cmd_env = get_base_env()

        with tempfile.NamedTemporaryFile(prefix=f'dnt_pil4dfs_{cmd[0]}_{get_inc_id()}_',
                                         suffix='.log',
                                         dir=self.conf.tmp_dir,
                                         delete=False) as log_file:
            log_name = log_file.name
            cmd_env['D_LOG_FILE'] = log_name

        cmd_env['DAOS_AGENT_DRPC_DIR'] = self.conf.agent_dir
        if report:
            cmd_env['D_IL_REPORT'] = '1'
        cmd_env['LD_PRELOAD'] = join(self.conf['PREFIX'], 'lib64', 'libpil4dfs.so')
        cmd_env['D_IL_NO_BYPASS'] = '1'
        if container is not None:
            # Create a temporary directory for the mount point, this will be removed as it goes out
            # scope so keep as a local for the rest of the function.
            # pylint: disable-next=consider-using-with
            tmp_dir = tempfile.TemporaryDirectory(prefix='pil4dfs_mount')
            cwd = tmp_dir.name
            cmd_env['D_IL_MOUNT_POINT'] = cwd
            cmd_env['D_IL_POOL'] = container.pool.id()
            cmd_env['D_IL_CONTAINER'] = container.id()
        else:
            cwd = None

        if self.conf.args.client_debug:
            cmd_env['D_LOG_MASK'] = self.conf.args.client_debug

        print('Run command: ')
        print(cmd)
        rc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, cwd=cwd,
                            env=cmd_env, check=False)
        print(rc)

        if rc.stderr != b'':
            print('Stderr from command')
            print(rc.stderr.decode('utf-8').strip())

        if rc.stdout != b'':
            print('Stdout from command')
            print(rc.stdout.decode('utf-8').strip())

        # if cwd and os.listdir(tmp_dir.name):
        #    print('Temporary directory is not empty')
        #    print(os.listdir(tmp_dir.name))
        #    assert False, 'Files left in tmp dir by pil4dfs'

        # Run log_test before other checks so this can warn for errors.
        log_test(self.conf, log_name)

        if check:
            assert rc.returncode == 0, rc

        if not report:
            return rc

        # check stderr for interception summary
        search = re.findall(r'\[op_sum\ ]  \d+', rc.stderr.decode('utf-8'))
        if len(search) == 0:
            raise NLTestFail('[op_sum ] is NOT found.')
        num_op = int(search[0][9:])
        if check and num_op == 0:
            raise NLTestFail('op_sum is zero. Unexpected.')
        print(f'DBG> num_op = {num_op}')
        return rc

    def set_fi(self, probability=0):
        """Run the client code to set server params"""
        cmd_env = get_base_env()

        cmd_env['D_INTERFACE'] = self.network_interface
        cmd_env['D_PROVIDER'] = self.network_provider
        valgrind_hdl = ValgrindHelper(self.conf)

        if self.conf.args.memcheck == 'no':
            valgrind_hdl.use_valgrind = False

        system_name = 'daos_server'

        exec_cmd = valgrind_hdl.get_cmd_prefix()

        agent_bin = join(self.conf['PREFIX'], 'bin', 'daos_agent')

        with tempfile.TemporaryDirectory(prefix='dnt_addr_',) as addr_dir:

            addr_file = join(addr_dir, f'{system_name}.attach_info_tmp')

            agent_cmd = [agent_bin,
                         '-i',
                         '-s',
                         self.agent_dir,
                         'dump-attachinfo',
                         '-o',
                         addr_file]

            rc = subprocess.run(agent_cmd, env=cmd_env, check=True)
            print(rc)

            # options here are: fault_id,max_faults,probability,err_code[,argument]
            cmd = ['set_fi_attr',
                   '--cfg_path',
                   addr_dir,
                   '--group-name',
                   'daos_server',
                   '--rank',
                   '0',
                   '--attr',
                   f'0,0,{probability},0,0']

            exec_cmd.append(join(self.conf['PREFIX'], 'bin', 'cart_ctl'))
            exec_cmd.extend(cmd)

            with tempfile.NamedTemporaryFile(prefix=f'dnt_crt_ctl_{get_inc_id()}_',
                                             suffix='.log',
                                             delete=False) as log_file:

                cmd_env['D_LOG_FILE'] = log_file.name
                cmd_env['DAOS_AGENT_DRPC_DIR'] = self.agent_dir

                rc = subprocess.run(exec_cmd,
                                    env=cmd_env,
                                    stdout=subprocess.PIPE,
                                    stderr=subprocess.PIPE,
                                    check=False)
                print(rc)
                valgrind_hdl.convert_xml()
                log_test(self.conf, log_file.name, show_memleaks=False)
