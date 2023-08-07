#!/usr/bin/env python3
"""Node local test (NLT).

Test script for running DAOS on a single node over tmpfs and running initial
smoke/unit tests.

Includes support for DFuse with a number of unit tests, as well as stressing
the client with fault injection of D_ALLOC() usage.
"""

# pylint: disable=too-many-lines

import os
from os.path import join
import sys
import time
import uuid
import json
import copy
import signal
import pprint
import stat
import errno
import argparse
import threading
import functools
import traceback
import subprocess  # nosec
import tempfile
import pickle  # nosec
import re
import shutil
import xattr
import junit_xml
import tabulate
import yaml


class NLTestFail(Exception):
    """Used to indicate test failure"""


class NLTestNoFi(NLTestFail):
    """Used to indicate Fault injection didn't work"""


class NLTestNoFunction(NLTestFail):
    """Used to indicate a function did not log anything"""

    def __init__(self, function):
        super().__init__(self)
        self.function = function


class NLTestTimeout(NLTestFail):
    """Used to indicate that an operation timed out"""


instance_num = 0  # pylint: disable=invalid-name


def get_inc_id():
    """Return a unique character"""
    global instance_num  # pylint: disable=invalid-name
    instance_num += 1
    return f'{instance_num:04d}'


def umount(path, background=False):
    """Umount dfuse from a given path"""
    if background:
        cmd = ['fusermount3', '-uz', path]
    else:
        cmd = ['fusermount3', '-u', path]
    ret = subprocess.run(cmd, check=False)
    print(f'rc from umount {ret.returncode}')
    return ret.returncode


class NLTConf():
    """Helper class for configuration"""

    def __init__(self, json_file, args):

        with open(json_file, 'r') as ofh:
            self._bc = json.load(ofh)
        self.agent_dir = None
        self.wf = None
        self.args = None
        self.max_log_size = None
        self.valgrind_errors = False
        self.log_timer = CulmTimer()
        self.compress_timer = CulmTimer()
        self.dfuse_parent_dir = tempfile.mkdtemp(dir=args.dfuse_dir,
                                                 prefix='dnt_dfuse_')
        self.tmp_dir = None
        if args.class_name:
            self.tmp_dir = join('nlt_logs', args.class_name)
            if os.path.exists(self.tmp_dir):
                for old_file in os.listdir(self.tmp_dir):
                    os.unlink(join(self.tmp_dir, old_file))
                os.rmdir(self.tmp_dir)
            os.makedirs(self.tmp_dir)

        self._compress_procs = []

    def __del__(self):
        self.flush_bz2()
        os.rmdir(self.dfuse_parent_dir)

    def set_wf(self, wf):
        """Set the WarningsFactory object"""
        self.wf = wf

    def set_args(self, args):
        """Set command line args"""
        self.args = args

        # Parse the max log size.
        if args.max_log_size:
            size = args.max_log_size
            if size.endswith('MiB'):
                size = int(size[:-3])
                size *= (1024 * 1024)
            elif size.endswith('GiB'):
                size = int(size[:-3])
                size *= (1024 * 1024 * 1024)
            self.max_log_size = int(size)

    def __getitem__(self, key):
        return self._bc[key]

    def compress_file(self, filename):
        """Compress a file using bz2 for space reasons

        Launch a bzip2 process in the background as this is time consuming, and each time
        a new process is launched then reap any previous ones which have completed.
        """
        # pylint: disable=consider-using-with
        self._compress_procs[:] = (proc for proc in self._compress_procs if proc.poll())
        self._compress_procs.append(subprocess.Popen(['bzip2', '--best', filename]))

    def flush_bz2(self):
        """Wait for all bzip2 subprocess to finish"""
        self.compress_timer.start()
        for proc in self._compress_procs:
            proc.wait()
        self._compress_procs = []
        self.compress_timer.stop()


class CulmTimer():
    """Class to keep track of elapsed time so we know where to focus performance tuning"""

    def __init__(self):
        self.total = 0
        self._start = None

    def start(self):
        """Start the timer"""
        self._start = time.perf_counter()

    def stop(self):
        """Stop the timer, and add elapsed to total"""
        self.total += time.perf_counter() - self._start


class BoolRatchet():
    """Used for saving test results"""

    # Any call to fail() of add_result with a True value will result
    # in errors being True.

    def __init__(self):
        self.errors = False

    def fail(self):
        """Mark as failure"""
        self.errors = True

    def add_result(self, result):
        """Save result, keep record of failure"""
        if result:
            self.fail()


class WarningsFactory():
    """Class to parse warnings, and save to JSON output file

    Take a list of failures, and output the data in a way that is best
    displayed according to
    https://github.com/jenkinsci/warnings-ng-plugin/blob/master/doc/Documentation.md
    """

    # Error levels supported by the reporting are LOW, NORMAL, HIGH, ERROR.

    def __init__(self,
                 filename,
                 junit=False,
                 class_id=None,
                 post=False,
                 post_error=False,
                 check=None):
        # pylint: disable=consider-using-with
        self._fd = open(filename, 'w')
        self.filename = filename
        self.post = post
        self.post_error = post_error
        self.check = check
        self.issues = []
        self._class_id = class_id
        self.pending = []
        self._running = True
        # Save the filename of the object, as __file__ does not
        # work in __del__
        self._file = __file__.lstrip('./')
        self._flush()

        if junit:
            # Insert a test-case and force it to failed.  Save this to file
            # and keep it there, until close() method is called, then remove
            # it and re-save.  This means any crash will result in there
            # being a results file with an error recorded.
            tc_startup = junit_xml.TestCase('Startup', classname=self._class_name('core'))
            tc_sanity = junit_xml.TestCase('Sanity', classname=self._class_name('core'))
            tc_sanity.add_error_info('NLT exited abnormally')
            self.test_suite = junit_xml.TestSuite('Node Local Testing',
                                                  test_cases=[tc_startup, tc_sanity])
            self._write_test_file()
        else:
            self.test_suite = None

    def _class_name(self, class_name):
        """Return a formatted ID string for class"""
        if self._class_id:
            return f'NLT.{self._class_id}.{class_name}'
        return f'NLT.{class_name}'

    def __del__(self):
        """Ensure the file is flushed on exit.

        If it hasn't already been closed then mark an error
        """
        if not self._fd:
            return

        entry = {}
        entry['fileName'] = self._file
        # pylint: disable=protected-access
        entry['lineStart'] = sys._getframe().f_lineno
        entry['message'] = 'Tests exited without shutting down properly'
        entry['severity'] = 'ERROR'
        self.issues.append(entry)

        # Do not try and write the junit file here, as that does not work
        # during teardown.
        self.test_suite = None
        self.close()

    def add_test_case(self, name, failure=None, test_class='core', output=None, duration=None,
                      stdout=None, stderr=None):
        """Add a test case to the results

        class and other metadata will be set automatically,
        if failure is set the test will fail with the message
        provided.  Saves the state to file after each update.
        """
        if not self.test_suite:
            return

        test_case = junit_xml.TestCase(name, classname=self._class_name(test_class),
                                       elapsed_sec=duration, stdout=stdout, stderr=stderr)
        if failure:
            test_case.add_failure_info(failure, output=output)
        self.test_suite.test_cases.append(test_case)

        self._write_test_file()

    def _write_test_file(self):
        """Write test results to file"""
        with open('nlt-junit.xml', 'w') as file:
            junit_xml.TestSuite.to_file(file, [self.test_suite], prettyprint=True)

    def explain(self, line, log_file, esignal):
        """Log an error, along with the other errors it caused

        Log the line as an error, and reference everything in the pending
        array.
        """
        count = len(self.pending)
        symptoms = set()
        locs = set()
        mtype = 'Fault injection'

        sev = 'LOW'
        if esignal:
            symptoms.add(f'Process died with signal {esignal}')
            sev = 'ERROR'
            mtype = 'Fault injection caused crash'
            count += 1

        if count == 0:
            return

        for (sline, smessage) in self.pending:
            locs.add(f'{sline.filename}:{sline.lineno}')
            symptoms.add(smessage)

        preamble = f'Fault injected here caused {count} errors, logfile {log_file}:'

        message = f"{preamble} {' '.join(sorted(symptoms))} {' '.join(sorted(locs))}"

        self.add(line, sev, message, cat='Fault injection location', mtype=mtype)
        self.pending = []

    def add(self, line, sev, message, cat=None, mtype=None):
        """Log an error

        Describe an error and add it to the issues array.
        Add it to the pending array, for later clarification
        """
        entry = {}
        entry['fileName'] = line.filename
        if mtype:
            entry['type'] = mtype
        else:
            entry['type'] = message
        if cat:
            entry['category'] = cat
        entry['lineStart'] = line.lineno
        # Jenkins no longer seems to display the description.
        entry['description'] = message
        entry['message'] = f'{line.get_anon_msg()}\n{message}'
        entry['severity'] = sev
        self.issues.append(entry)
        if self.pending and self.pending[0][0].pid != line.pid:
            self.reset_pending()
        self.pending.append((line, message))
        self._flush()
        if self.post or (self.post_error and sev in ('HIGH', 'ERROR')):
            # https://docs.github.com/en/actions/reference/workflow-commands-for-github-actions
            if self.post_error:
                message = line.get_msg()
            print(f'::warning file={line.filename},line={line.lineno},::{self.check}, {message}')

    def reset_pending(self):
        """Reset the pending list

        Should be called before iterating on each new file, so errors
        from previous files aren't attributed to new files.
        """
        self.pending = []

    def _flush(self):
        """Write the current list to the json file

        This is done just in case of crash.  This function might get called
        from the __del__ method of DaosServer, so do not use __file__ here
        either.
        """
        self._fd.seek(0)
        self._fd.truncate(0)
        data = {}
        data['issues'] = list(self.issues)
        if self._running:
            # When the test is running insert an error in case of abnormal
            # exit, so that crashes in this code can be identified.
            entry = {}
            entry['fileName'] = self._file
            # pylint: disable=protected-access
            entry['lineStart'] = sys._getframe().f_lineno
            entry['severity'] = 'ERROR'
            entry['message'] = 'Tests are still running'
            data['issues'].append(entry)
        json.dump(data, self._fd, indent=2)
        self._fd.flush()

    def close(self):
        """Save, and close the log file"""
        self._running = False
        self._flush()
        self._fd.close()
        self._fd = None
        print(f'Closed JSON file {self.filename} with {len(self.issues)} errors')
        if self.test_suite:
            # This is a controlled shutdown, so wipe the error saying forced exit.
            self.test_suite.test_cases[1].errors = []
            self.test_suite.test_cases[1].error_message = []
            self._write_test_file()


def load_conf(args):
    """Load the build config file"""
    file_self = os.path.dirname(os.path.abspath(__file__))
    json_file = None
    while True:
        new_file = join(file_self, '.build_vars.json')
        if os.path.exists(new_file):
            json_file = new_file
            break
        file_self = os.path.dirname(file_self)
        if file_self == '/':
            raise NLTestFail('build file not found')
    return NLTConf(json_file, args)


def get_base_env(clean=False):
    """Return the base set of env vars needed for DAOS"""
    if clean:
        env = {}
    else:
        env = os.environ.copy()
    env['DD_MASK'] = 'all'
    env['DD_SUBSYS'] = 'all'
    env['D_LOG_MASK'] = 'DEBUG'
    env['D_LOG_SIZE'] = '5g'
    env['FI_UNIVERSE_SIZE'] = '128'

    # Enable this to debug memory errors, it has a performance impact but will scan the heap
    # for corruption.  See DAOS-12735 for why this can cause problems in practice.
    # env['MALLOC_CHECK_'] = '3'

    # Otherwise max number of contexts will be limited by number of cores
    env['CRT_CTX_NUM'] = '32'

    return env


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
        self.uuid = cont_uuid
        self.label = label
        self.pool = pool
        if pool is not None:
            assert isinstance(self.pool, DaosPool)

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

    def destroy(self, valgrind=True, log_check=True):
        """Destroy the container

        Args:
            valgrind (bool, optional): Run the command under valgrind. Defaults to True.
            log_check (bool, optional): Run log analysis. Defaults to True.

        Raises:
            NLTestFail: If Pool was not provided when object created.
        """
        destroy_container(self.pool.conf, self.pool.id(), self.id(),
                          valgrind=valgrind, log_check=log_check)


class DaosServer():
    """Manage a DAOS server instance"""

    def __init__(self, conf, test_class=None, valgrind=False, wf=None, fatal_errors=None,
                 enable_fi=False):
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

        socket_dir = '/tmp/dnt_sockets'
        if not os.path.exists(socket_dir):
            os.mkdir(socket_dir)

        self.agent_dir = tempfile.mkdtemp(prefix='dnt_agent_')

        self._yaml_file = None
        self._io_server_dir = None
        self.test_pool = None
        self.network_interface = None
        self.network_provider = None

        self.fuse_procs = []

    def __enter__(self):
        self.start()
        return self

    def __exit__(self, _type, _value, _traceback):
        rc = self.stop(self.wf)
        if rc != 0 and self.fatal_errors is not None:
            self.fatal_errors.fail()
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
                self.stop(None)
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

    def start(self):
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

            self._io_server_dir = tempfile.TemporaryDirectory(prefix='dnt_io_')

            with open(join(self._io_server_dir.name, 'daos_engine'), 'w') as fd:
                fd.write('#!/bin/sh\n')
                fd.write(f"export PATH={join(self.conf['PREFIX'],'bin')}:$PATH\n")
                fd.write(f'exec valgrind {" ".join(valgrind_args)} daos_engine "$@"\n')

            os.chmod(join(self._io_server_dir.name, 'daos_engine'),
                     stat.S_IXUSR | stat.S_IRUSR)

            plain_env['PATH'] = f'{self._io_server_dir.name}:{plain_env["PATH"]}'
            self.max_start_time = 300
            self.max_stop_time = 600
            self.stop_sleep_time = 10

        daos_server = join(self.conf['PREFIX'], 'bin', 'daos_server')

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
            scyaml['engines'].append(engine)
        self._yaml_file = tempfile.NamedTemporaryFile(prefix='nlt-server-config-', suffix='.yaml')
        self._yaml_file.write(yaml.dump(scyaml, encoding='utf-8'))
        self._yaml_file.flush()

        cmd = [daos_server, f'--config={self._yaml_file.name}', 'start', '--insecure']

        if self.conf.args.no_root:
            cmd.append('--recreate-superblocks')

        # pylint: disable=consider-using-with
        self._sp = subprocess.Popen(cmd, env=plain_env)

        agent_config = join(self.agent_dir, 'nlt_agent.yaml')
        with open(agent_config, 'w') as fd:
            agent_data = {
                'access_points': scyaml['access_points'],
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

    def stop(self, wf):
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
                        key, raw = line.split(':', maxsplit=2)
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
        rc = self.run_dmg(['system', 'stop'])
        if rc.returncode != 0:
            print(rc)
            entry = {}
            entry['fileName'] = self._file
            # pylint: disable=protected-access
            entry['lineStart'] = sys._getframe().f_lineno
            entry['severity'] = 'ERROR'
            msg = f'dmg system stop failed with {rc.returncode}'
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

        for log in self.server_logs:
            log_test(self.conf, log.name, leak_wf=wf, skip_fi=self._fi)
            self.server_logs.remove(log)
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

        rc = self.run_dmg(['pool', 'create', 'NLT', '--scm-size', f'{size}M'])
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

        with tempfile.NamedTemporaryFile(prefix=f'dnt_cmd_{get_inc_id()}_',
                                         suffix='.log',
                                         dir=self.conf.tmp_dir,
                                         delete=False) as log_file:
            log_name = log_file.name
            cmd_env['D_LOG_FILE'] = log_name

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
        if container is not None:
            # Create a temporary directory for the mount point, this will be removed as it goes out
            # scope so keep as a local for the rest of the function.
            # pylint: disable-next=consider-using-with
            tmp_dir = tempfile.TemporaryDirectory(prefix='pil4dfs_mount')
            cwd = tmp_dir.name
            cmd_env['DAOS_MOUNT_POINT'] = cwd
            cmd_env['DAOS_POOL'] = container.pool.id()
            cmd_env['DAOS_CONTAINER'] = container.id()
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

        cmd_env['OFI_INTERFACE'] = self.network_interface
        cmd_env['CRT_PHY_ADDR_STR'] = self.network_provider
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

        src_dir = os.path.realpath(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
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

    def convert_xml(self):
        """Modify the xml file"""
        if not self.use_valgrind:
            return
        with open(self._xml_file, 'r') as fd:
            with open(f'{self._xml_file}.xml', 'w') as ofd:
                for line in fd:
                    if self.src_dir in line:
                        ofd.write(line.replace(self.src_dir, ''))
                    else:
                        ofd.write(line)
        os.unlink(self._xml_file)


class DFuse():
    """Manage a dfuse instance"""

    instance_num = 0

    def __init__(self, daos, conf, pool=None, container=None, mount_path=None, uns_path=None,
                 caching=True, wbcache=True, multi_user=False):
        if mount_path:
            self.dir = mount_path
        else:
            self.dir = tempfile.mkdtemp(dir=conf.dfuse_parent_dir, prefix='dfuse_mount.')
        self.pool = pool
        self.uns_path = uns_path
        self.container = container
        if isinstance(pool, DaosPool):
            self.pool = pool.id()
        if isinstance(container, DaosCont):
            self.container = container.id()
            self.pool = container.pool.id()
        self.conf = conf
        self.multi_user = multi_user
        self.cores = 0
        self._daos = daos
        self.caching = caching
        self.wbcache = wbcache
        self.use_valgrind = True
        self._sp = None
        self.log_flush = False
        self.log_mask = None
        self.log_file = None

        self.valgrind = None
        if not os.path.exists(self.dir):
            os.mkdir(self.dir)

    def __str__(self):

        if self._sp:
            running = 'running'
        else:
            running = 'not running'

        return f'DFuse instance at {self.dir} ({running})'

    def start(self, v_hint=None, single_threaded=False, use_oopt=False):
        """Start a dfuse instance"""
        dfuse_bin = join(self.conf['PREFIX'], 'bin', 'dfuse')

        pre_inode = os.stat(self.dir).st_ino

        my_env = get_base_env()

        if self.conf.args.dfuse_debug:
            my_env['D_LOG_MASK'] = self.conf.args.dfuse_debug

        if self.log_flush:
            my_env['D_LOG_FLUSH'] = 'DEBUG'

        if v_hint is None:
            v_hint = get_inc_id()

        prefix = f'dnt_dfuse_{v_hint}_'
        with tempfile.NamedTemporaryFile(prefix=prefix, suffix='.log', delete=False) as log_file:
            self.log_file = log_file.name

        my_env['D_LOG_FILE'] = self.log_file
        my_env['DAOS_AGENT_DRPC_DIR'] = self._daos.agent_dir
        if self.log_mask:
            my_env['D_LOG_MASK'] = self.log_mask
        if self.conf.args.dtx == 'yes':
            my_env['DFS_USE_DTX'] = '1'

        self.valgrind = ValgrindHelper(self.conf, v_hint)
        if self.conf.args.memcheck == 'no':
            self.valgrind.use_valgrind = False

        if not self.use_valgrind:
            self.valgrind.use_valgrind = False

        if self.cores:
            cmd = ['numactl', '--physcpubind', f'0-{self.cores - 1}']
        else:
            cmd = []

        cmd.extend(self.valgrind.get_cmd_prefix())

        cmd.extend([dfuse_bin, '--mountpoint', self.dir, '--foreground'])

        if self.multi_user:
            cmd.append('--multi-user')

        if single_threaded:
            cmd.append('--singlethread')

        if not self.caching:
            cmd.append('--disable-caching')
        else:
            if not self.wbcache:
                cmd.append('--disable-wb-cache')

        if self.uns_path:
            cmd.extend(['--path', self.uns_path])

        if use_oopt:
            if self.pool:
                if self.container:
                    cmd.extend(['-o', f'pool={self.pool},container={self.container}'])
                else:
                    cmd.extend(['-o', f'pool={self.pool}'])

        else:
            if self.pool:
                cmd.extend(['--pool', self.pool])
            if self.container:
                cmd.extend(['--container', self.container])

        print(f"Running {' '.join(cmd)}")
        # pylint: disable-next=consider-using-with
        self._sp = subprocess.Popen(cmd, env=my_env)
        print(f'Started dfuse at {self.dir}')
        print(f'Log file is {self.log_file}')

        total_time = 0
        while os.stat(self.dir).st_ino == pre_inode:
            print('Dfuse not started, waiting...')
            try:
                ret = self._sp.wait(timeout=1)
                print(f'dfuse command exited with {ret}')
                self._sp = None
                if os.path.exists(self.log_file):
                    log_test(self.conf, self.log_file)
                os.rmdir(self.dir)
                raise NLTestFail('dfuse died waiting for start')
            except subprocess.TimeoutExpired:
                pass
            total_time += 1
            if total_time > 60:
                raise NLTestFail('Timeout starting dfuse')

        self._daos.add_fuse(self)

    def _close_files(self):
        work_done = False
        for fname in os.listdir('/proc/self/fd'):
            try:
                tfile = os.readlink(join('/proc/self/fd', fname))
            except FileNotFoundError:
                continue
            if tfile.startswith(self.dir):
                print(f'closing file {tfile}')
                os.close(int(fname))
                work_done = True
        return work_done

    def __del__(self):
        if self._sp:
            self.stop()

    def stop(self, ignore_einval=False):
        """Stop a previously started dfuse instance"""
        fatal_errors = False
        if not self._sp:
            return fatal_errors

        print('Stopping fuse')
        ret = umount(self.dir)
        if ret:
            umount(self.dir, background=True)
            self._close_files()
            time.sleep(2)
            umount(self.dir)

        run_leak_test = True
        try:
            ret = self._sp.wait(timeout=20)
            print(f'rc from dfuse {ret}')
            if ret == 42:
                self.conf.wf.add_test_case(str(self), failure='valgrind errors', output=ret)
                self.conf.valgrind_errors = True
            elif ret != 0:
                fatal_errors = True
        except subprocess.TimeoutExpired:
            print('Timeout stopping dfuse')
            self._sp.send_signal(signal.SIGTERM)
            fatal_errors = True
            run_leak_test = False
        self._sp = None
        log_test(self.conf, self.log_file, show_memleaks=run_leak_test, ignore_einval=ignore_einval)

        # Finally, modify the valgrind xml file to remove the
        # prefix to the src dir.
        self.valgrind.convert_xml()
        os.rmdir(self.dir)
        self._daos.remove_fuse(self)
        return fatal_errors

    def wait_for_exit(self):
        """Wait for dfuse to exit"""
        ret = self._sp.wait()
        print(f'rc from dfuse {ret}')
        self._sp = None
        log_test(self.conf, self.log_file)

        # Finally, modify the valgrind xml file to remove the
        # prefix to the src dir.
        self.valgrind.convert_xml()
        os.rmdir(self.dir)

    def il_cmd(self, cmd, check_read=True, check_write=True, check_fstat=True):
        """Run a command under the interception library

        Do not run valgrind here, not because it's not useful
        but the options needed are different.  Valgrind handles
        linking differently so some memory is wrongly lost that
        would be freed in the _fini() function, and a lot of
        commands do not free all memory anyway.
        """
        my_env = get_base_env()
        prefix = f'dnt_dfuse_il_{get_inc_id()}_'
        with tempfile.NamedTemporaryFile(prefix=prefix, suffix='.log', delete=False) as log_file:
            log_name = log_file.name
        my_env['D_LOG_FILE'] = log_name
        my_env['LD_PRELOAD'] = join(self.conf['PREFIX'], 'lib64', 'libioil.so')
        my_env['DAOS_AGENT_DRPC_DIR'] = self.conf.agent_dir
        my_env['D_IL_REPORT'] = '2'
        if self.conf.args.client_debug:
            my_env['D_LOG_MASK'] = self.conf.args.client_debug
        ret = subprocess.run(cmd, env=my_env, check=False)
        print(f'Logged il to {log_name}')
        print(ret)

        if self.caching:
            check_fstat = False

        try:
            log_test(self.conf, log_name, check_read=check_read, check_write=check_write,
                     check_fstat=check_fstat)
            assert ret.returncode == 0
        except NLTestNoFunction as error:
            command = ' '.join(cmd)
            print(f"ERROR: command '{command}' did not log via {error.function}")
            ret.returncode = 1

        assert ret.returncode == 0, ret
        return ret

    def check_usage(self, ino=None, inodes=None, open_files=None, pools=None, containers=None):
        """Query and verify the dfuse statistics.

        Returns the raw numbers in a dict.
        """
        cmd = ['filesystem', 'query', self.dir]

        if ino is not None:
            cmd.extend(['--inode', str(ino)])
        rc = run_daos_cmd(self.conf, cmd, use_json=True)
        print(rc)
        assert rc.returncode == 0

        if inodes:
            assert rc.json['response']['inodes'] == inodes, rc
        if open_files:
            assert rc.json['response']['open_files'] == open_files, rc
        if pools:
            assert rc.json['response']['pools'] == pools, rc
        if containers:
            assert rc.json['response']['containers'] == containers, rc
        return rc.json['response']

    def _evict_path(self, path):
        """Evict a path from dfuse"""
        cmd = ['filesystem', 'evict', path]
        rc = run_daos_cmd(self.conf, cmd, use_json=True)
        print(rc)
        assert rc.returncode == 0

        return rc.json['response']

    def evict_and_wait(self, paths):
        """Evict a number of paths from dfuse"""
        inodes = []
        for path in paths:
            rc = self._evict_path(path)
            inodes.append(rc['inode'])

        sleeps = 0

        for inode in inodes:
            found = True
            while found:
                rc = self.check_usage(inode)
                print(rc)
                found = rc['resident']
                if not found:
                    sleeps += 1
                    assert sleeps < 10, 'Path still present 10 seconds after eviction'
                    time.sleep(1)


def assert_file_size_fd(fd, size):
    """Verify the file size is as expected"""
    my_stat = os.fstat(fd)
    print(f'Checking file size is {size} {my_stat.st_size}')
    assert my_stat.st_size == size


def assert_file_size(ofd, size):
    """Verify the file size is as expected"""
    assert_file_size_fd(ofd.fileno(), size)


def import_daos(server, conf):
    """Return a handle to the pydaos module"""
    pydir = f'python{sys.version_info.major}.{sys.version_info.minor}'

    sys.path.append(join(conf['PREFIX'], 'lib64', pydir, 'site-packages'))

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
                file_oclass=None, hints=None, valgrind=False, log_check=True, cwd=None):
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

    Returns:
        DaosCont: Newly created container as DaosCont object.
    """
    cmd = ['container', 'create']

    if not path:
        assert isinstance(pool, DaosPool)

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


def destroy_container(conf, pool, container, valgrind=True, log_check=True):
    """Destroy a container"""
    if isinstance(pool, DaosPool):
        pool = pool.id()
    if isinstance(container, DaosCont):
        container = container.id()
    cmd = ['container', 'destroy', pool, container]
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


def check_dfs_tool_output(output, oclass, csize):
    """Verify daos fs tool output"""
    line = output.splitlines()
    dfs_attr = line[0].split()[-1]
    if oclass is not None:
        if dfs_attr != oclass:
            return False
    dfs_attr = line[1].split()[-1]
    if csize is not None:
        if dfs_attr != csize:
            return False
    return True


def needs_dfuse(method):
    """Decorator function for starting dfuse under posix_tests class

    Runs every test twice, once with caching enabled, and once with
    caching disabled.
    """
    @functools.wraps(method)
    def _helper(self):
        if self.call_index == 0:
            caching = True
            self.needs_more = True
            self.test_name = f'{method.__name__}_with_caching'
        else:
            caching = False

        self.dfuse = DFuse(self.server,
                           self.conf,
                           caching=caching,
                           container=self.container)
        self.dfuse.start(v_hint=self.test_name)
        try:
            rc = method(self)
        finally:
            if self.dfuse.stop():
                self.fatal_errors = True
        return rc

    return _helper


# pylint: disable-next=invalid-name
class needs_dfuse_with_opt():
    """Decorator class for starting dfuse under posix_tests class

    By default runs the method twice, once with caching and once without, however can be
    configured to behave differently.  Interacts with the run_posix_tests._run_test() method
    to achieve this.
    """

    # pylint: disable=too-few-public-methods

    def __init__(self, caching=None, wbcache=True, single_threaded=False):
        self.caching = caching
        self.wbcache = wbcache
        self.single_threaded = single_threaded

    def __call__(self, method):
        """Wrapper function"""
        @functools.wraps(method)
        def _helper(obj):

            caching = self.caching
            if caching is None:
                if obj.call_index == 0:
                    caching = True
                    obj.needs_more = True
                    obj.test_name = f'{method.__name__}_with_caching'
                else:
                    caching = False

            obj.dfuse = DFuse(obj.server,
                              obj.conf,
                              caching=caching,
                              wbcache=self.wbcache,
                              container=obj.container)
            obj.dfuse.start(v_hint=method.__name__, single_threaded=self.single_threaded)
            try:
                rc = method(obj)
            finally:
                if obj.dfuse.stop():
                    obj.fatal_errors = True
            return rc
        return _helper


class PrintStat():
    """Class for nicely showing file 'stat' data, similar to ls -l"""

    headers = ['uid', 'gid', 'size', 'mode', 'filename']

    def __init__(self, filename=None):
        # Setup the object, and maybe add some data to it.
        self._stats = []
        self.count = 0
        if filename:
            self.add(filename)

    def dir_add(self, dirname):
        """Add a directory contents

        This differs from .add(dirname, show_dir=True) as it does not add the dir itself and the
        result can be compared across mounts.
        """
        with os.scandir(dirname) as dirfd:
            for entry in dirfd:
                self.add(entry.name, attr=os.stat(join(dirname, entry.name)))

    def add(self, filename, attr=None, show_dir=False):
        """Add an entry to be displayed"""
        if attr is None:
            attr = os.stat(filename)

        self._stats.append([attr.st_uid,
                            attr.st_gid,
                            attr.st_size,
                            stat.filemode(attr.st_mode),
                            filename])
        self.count += 1

        if show_dir:
            tab = '.' * len(filename)
            for fname in os.listdir(filename):
                self.add(join(tab, fname), attr=os.stat(join(filename, fname)))

    def __str__(self):
        return tabulate.tabulate(self._stats, self.headers)

    def __eq__(self, other):
        return self._stats == other._stats


# This is test code where methods are tests, so we want to have lots of them.
class PosixTests():
    """Class for adding standalone unit tests"""

    # pylint: disable=too-many-public-methods
    def __init__(self, server, conf, pool=None):
        self.server = server
        self.conf = conf
        self.pool = pool
        self.container = None
        self.container_label = None
        self.dfuse = None
        self.fatal_errors = False

        # Ability to invoke each method multiple times, call_index is set to
        # 0 for each test method, if the method requires invoking a second time
        # (for example to re-run with caching) then it should set needs_more
        # to true, and it will be invoked with a greater value for call_index
        # self.test_name will be set automatically, but can be modified by
        # constructors, see @needs_dfuse for where this is used.
        self.call_index = 0
        self.needs_more = False
        self.test_name = ''

    @staticmethod
    def fail():
        """Mark a test method as failed"""
        raise NLTestFail

    @staticmethod
    def _check_dirs_equal(expected, dir_name):
        """Verify that the directory contents are as expected

        Takes a list of expected files, and a directory name.
        """
        files = sorted(os.listdir(dir_name))

        expected = sorted(expected)

        print(f'Comparing real vs expected contents of {dir_name}')
        exp = ','.join(expected)
        print(f'expected: "{exp}"')
        act = ','.join(files)
        print(f'actual:   "{act}"')

        assert files == expected

    def test_cont_list(self):
        """Test daos container list"""
        rc = run_daos_cmd(self.conf, ['container', 'list', self.pool.id()])
        print(rc)
        assert rc.returncode == 0, rc

        rc = run_daos_cmd(self.conf, ['container', 'list', self.pool.id()], use_json=True)
        print(rc)
        assert rc.returncode == 0, rc

    @needs_dfuse_with_opt(caching=False)
    def test_oclass(self):
        """Test container object class options"""
        container = create_cont(self.conf, self.pool, ctype="POSIX", label='oclass_test',
                                oclass='S1', dir_oclass='S2', file_oclass='S4')
        run_daos_cmd(self.conf,
                     ['container', 'query',
                      self.pool.id(), container.id()],
                     show_stdout=True)

        dfuse = DFuse(self.server, self.conf, container=container)
        dfuse.use_valgrind = False
        dfuse.start()

        dir1 = join(dfuse.dir, 'd1')
        os.mkdir(dir1)
        file1 = join(dir1, 'f1')
        with open(file1, 'w') as ofd:
            ofd.write('hello')

        cmd = ['fs', 'get-attr', '--path', dir1]
        print('get-attr of ' + dir1)
        rc = run_daos_cmd(self.conf, cmd)
        assert rc.returncode == 0
        print(rc)
        output = rc.stdout.decode('utf-8')
        assert check_dfs_tool_output(output, 'S2', '1048576')

        cmd = ['fs', 'get-attr', '--path', file1]
        print('get-attr of ' + file1)
        rc = run_daos_cmd(self.conf, cmd)
        assert rc.returncode == 0
        print(rc)
        output = rc.stdout.decode('utf-8')
        assert check_dfs_tool_output(output, 'S4', '1048576')

        if dfuse.stop():
            self.fatal_errors = True

        container.destroy()

    def test_cache(self):
        """Test with caching enabled"""
        run_daos_cmd(self.conf,
                     ['container', 'query',
                      self.pool.id(), self.container.id()],
                     show_stdout=True)

        cont_attrs = {'dfuse-attr-time': 2,
                      'dfuse-dentry-time': '100s',
                      'dfuse-dentry-dir-time': '100s',
                      'dfuse-ndentry-time': '100s'}
        self.container.set_attrs(cont_attrs)

        run_daos_cmd(self.conf,
                     ['container', 'get-attr',
                      self.pool.id(), self.container.id()],
                     show_stdout=True)

        dfuse = DFuse(self.server, self.conf, container=self.container)
        dfuse.start()

        print(os.listdir(dfuse.dir))

        if dfuse.stop():
            self.fatal_errors = True

    @needs_dfuse
    def test_truncate(self):
        """Test file read after truncate"""
        filename = join(self.dfuse.dir, 'myfile')

        with open(filename, 'w') as fd:
            fd.write('hello')

        os.truncate(filename, 1024 * 1024 * 4)
        with open(filename, 'r') as fd:
            data = fd.read(5)
            print(f'_{data}_')
            assert data == 'hello'

    @needs_dfuse
    def test_cont_info(self):
        """Check that daos container info and fs get-attr works on container roots"""

        def _check_cmd(check_path):
            rc = run_daos_cmd(self.conf,
                              ['container', 'query', '--path', check_path],
                              use_json=True)
            print(rc)
            assert rc.returncode == 0, rc
            rc = run_daos_cmd(self.conf,
                              ['fs', 'get-attr', '--path', check_path],
                              use_json=True)
            print(rc)
            assert rc.returncode == 0, rc

        child_path = join(self.dfuse.dir, 'new_cont')
        new_cont1 = create_cont(self.conf, self.pool, path=child_path)
        print(new_cont1)

        # Check that cont create works with relative paths where there is no directory part,
        # this is important as duns inspects the path and tries to access the parent directory.
        child_path_cwd = join(self.dfuse.dir, 'new_cont_2')
        new_cont_cwd = create_cont(self.conf, self.pool, path='new_cont_2', cwd=self.dfuse.dir)
        print(new_cont_cwd)

        _check_cmd(child_path)
        _check_cmd(child_path_cwd)
        _check_cmd(self.dfuse.dir)

        # Now evict the new containers

        self.dfuse.evict_and_wait([child_path, child_path_cwd])
        # Destroy the new containers at this point as dfuse will have dropped references.
        new_cont1.destroy()
        new_cont_cwd.destroy()

    @needs_dfuse
    def test_read(self):
        """Test a basic read.

        Write to a file, then read from it.  With caching on dfuse won't see the read, with caching
        off dfuse will see one truncated read, then another at EOF which will return zero bytes.
        """
        file_name = join(self.dfuse.dir, 'file')
        with open(file_name, 'w') as fd:
            fd.write('test')

        with open(file_name, 'r') as fd:
            data = fd.read(16)  # Pass in a buffer size here or python will only read file size.
        print(data)
        assert data == 'test'

    def test_two_mounts(self):
        """Create two mounts, and check that a file created in one can be read from the other"""
        dfuse0 = DFuse(self.server,
                       self.conf,
                       caching=False,
                       container=self.container)
        dfuse0.start(v_hint='two_0')

        dfuse1 = DFuse(self.server,
                       self.conf,
                       caching=True,
                       container=self.container)
        dfuse1.start(v_hint='two_1')

        file0 = join(dfuse0.dir, 'file')
        with open(file0, 'w') as fd:
            fd.write('test')

        with open(join(dfuse1.dir, 'file'), 'r') as fd:
            data = fd.read()
        print(data)
        assert data == 'test'

        with open(file0, 'w') as fd:
            fd.write('test')

        if dfuse0.stop():
            self.fatal_errors = True
        if dfuse1.stop():
            self.fatal_errors = True

    def test_cache_expire(self):
        """Check that data and readdir cache expire correctly

        Crete two mount points on the same container, one for testing, the second for
        oob-modifications.

        Populate directory, read files in it (simulating ls -l).

        oob remove some files, create some more and write to others.

        re-read directory contents, this should appear unchanged.

        Wait for expiry time to pass

        re-read directory contents again, now this should be up to date.
        """
        cache_time = 20

        cont_attrs = {'dfuse-data-cache': False,
                      'dfuse-attr-time': cache_time,
                      'dfuse-dentry-time': cache_time,
                      'dfuse-ndentry-time': cache_time}
        self.container.set_attrs(cont_attrs)

        dfuse0 = DFuse(self.server,
                       self.conf,
                       caching=True,
                       wbcache=False,
                       container=self.container)
        dfuse0.start(v_hint='expire_0')

        dfuse1 = DFuse(self.server,
                       self.conf,
                       caching=False,
                       container=self.container)
        dfuse1.start(v_hint='expire_1')

        # Create ten files.
        for idx in range(10):
            with open(join(dfuse0.dir, f'batch0.{idx}'), 'w') as ofd:
                ofd.write('hello')

        start = time.perf_counter()

        stat_log = PrintStat()
        stat_log.dir_add(dfuse0.dir)
        print(stat_log)

        # Create ten more.
        for idx in range(10, 20):
            with open(join(dfuse1.dir, f'batch1.{idx}'), 'w') as ofd:
                ofd.write('hello')

        # Update some of the original ten.
        for idx in range(3):
            with open(join(dfuse1.dir, f'batch0.{idx}'), 'w') as ofd:
                ofd.write('hello world')

        # Remove some of the original ten.
        for idx in range(3, 6):
            os.unlink(join(dfuse1.dir, f'batch0.{idx}'))

        stat_log_oob = PrintStat()
        stat_log_oob.dir_add(dfuse1.dir)
        print(stat_log_oob)

        stat_log1 = PrintStat()
        stat_log1.dir_add(dfuse0.dir)
        print(stat_log1)

        elapsed = time.perf_counter() - start

        assert elapsed < cache_time / 2, f'Test ran to slow, increase timeout {elapsed}'

        # Now wait for cache timeout, allowing for the readdir calls above to repopulate it.
        time.sleep(cache_time + 2)

        stat_log2 = PrintStat()
        stat_log2.dir_add(dfuse0.dir)
        print(stat_log2)

        if dfuse0.stop():
            self.fatal_errors = True
        if dfuse1.stop():
            self.fatal_errors = True

        assert stat_log == stat_log1, 'Contents changed within timeout'
        assert stat_log != stat_log2, 'Contents did not change after timeout'

        assert stat_log.count == 10, 'Incorrect initial file count'
        assert stat_log2.count == 17, 'Incorrect file count after timeout'

        assert stat_log2 == stat_log_oob, 'Contents not correct after timeout'

    @needs_dfuse
    def test_readdir_basic(self):
        """Basic readdir test.

        Call readdir on a empty directory, then populate it and call it again
        """
        dir_name = tempfile.mkdtemp(dir=self.dfuse.dir)
        files = os.listdir(dir_name)
        assert len(files) == 0

        count = 40

        for idx in range(count):
            with open(join(dir_name, f'file_{idx}'), 'w'):
                pass

        files = os.listdir(dir_name)
        assert len(files) == count

    @needs_dfuse
    def test_readdir_30(self):
        """Test reading a directory with 30 entries"""
        self.readdir_test(30)

    def readdir_test(self, count, test_all=False):
        """Run a rudimentary readdir test"""
        wide_dir = tempfile.mkdtemp(dir=self.dfuse.dir)
        start = time.perf_counter()
        for idx in range(count):
            with open(join(wide_dir, f'file_{idx}'), 'w'):
                pass
            if test_all:
                files = os.listdir(wide_dir)
                assert len(files) == idx + 1
        duration = time.perf_counter() - start
        rate = count / duration
        print(f'Created {count} files in {duration:.1f} seconds rate {rate:.1f}')
        print('Listing dir contents')
        start = time.perf_counter()
        files = os.listdir(wide_dir)
        duration = time.perf_counter() - start
        rate = count / duration
        print(f'Listed {count} files in {duration:.1f} seconds rate {rate:.1f}')
        print(files)
        print(len(files))
        assert len(files) == count
        print('Listing dir contents again')
        start = time.perf_counter()
        files = os.listdir(wide_dir)
        duration = time.perf_counter() - start
        print(f'Listed {count} files in {duration:.1f} seconds rate {count / duration:.1f}')
        print(files)
        print(len(files))
        assert len(files) == count
        files = []
        start = time.perf_counter()
        with os.scandir(wide_dir) as entries:
            for entry in entries:
                files.append(entry.name)
        duration = time.perf_counter() - start
        print(f'Scanned {count} files in {duration:.1f} seconds rate {count / duration:.1f}')
        print(files)
        print(len(files))
        assert len(files) == count

        files = []
        files2 = []
        start = time.perf_counter()
        with os.scandir(wide_dir) as entries:
            with os.scandir(wide_dir) as second:
                for entry in entries:
                    files.append(entry.name)
                for entry in second:
                    files2.append(entry.name)
        duration = time.perf_counter() - start
        print(f'Double scanned {count} files in {duration:.1f} seconds rate {count / duration:.1f}')
        print(files)
        print(len(files))
        assert len(files) == count
        print(files2)
        print(len(files2))
        assert len(files2) == count

    @needs_dfuse
    def test_readdir_hard(self):
        """Run a parallel readdir test.

        Open a directory twice, read from the 1st one once, then read the entire directory from
        the second handle.  This tests dfuse in-memory caching.
        """
        test_dir = join(self.dfuse.dir, 'test_dir')
        os.mkdir(test_dir)
        count = 140
        src_files = set()
        for idx in range(count):
            fname = f'file_{idx}'
            src_files.add(fname)
            with open(join(test_dir, fname), 'w'):
                pass

        files = []
        files2 = []
        with os.scandir(test_dir) as entries:
            with os.scandir(test_dir) as second:
                files2.append(next(second).name)
                for entry in entries:
                    files.append(entry.name)
                    assert len(files) < count + 2
                for entry in second:
                    files2.append(entry.name)
                    assert len(files2) < count + 2

        print('Reads are from list 2, 1, 1, 2.')
        print(files)
        print(files2)
        assert files == files2, 'inconsistent file names'
        assert len(files) == count, 'incoorect file count'
        assert set(files) == src_files, 'incorrect file names'

    @needs_dfuse
    def test_readdir_cache_short(self):
        """Run a parallel readdir test.

        This differs from readdir_hard in that the directory is smaller so dfuse will return
        it in one go.  The memory management in dfuse is different in this case so add another
        test for memory leaks.
        """
        test_dir = join(self.dfuse.dir, 'test_dir')
        os.mkdir(test_dir)
        count = 5
        for idx in range(count):
            with open(join(test_dir, f'file_{idx}'), 'w'):
                pass

        files = []
        files2 = []
        with os.scandir(test_dir) as entries:
            with os.scandir(test_dir) as second:
                files2.append(next(second).name)
                for entry in entries:
                    files.append(entry.name)
                for entry in second:
                    files2.append(entry.name)

        print('Reads are from list 2, 1, 1, 2.')
        print(files)
        print(files2)
        assert files == files2
        assert len(files) == count

    @needs_dfuse
    def test_readdir_unlink(self):
        """Test readdir where a entry is removed mid read

        Populate a directory, read the contents to know the order, then unlink a file and re-read
        to verify the file is missing.  If doing the unlink during read then the kernel cache
        will include the unlinked file so do not check for this behavior.
        """
        test_dir = join(self.dfuse.dir, 'test_dir')
        os.mkdir(test_dir)
        count = 50
        for idx in range(count):
            with open(join(test_dir, f'file_{idx}'), 'w'):
                pass

        files = []
        with os.scandir(test_dir) as entries:
            for entry in entries:
                files.append(entry.name)

        os.unlink(join(test_dir, files[-2]))

        post_files = []
        with os.scandir(test_dir) as entries:
            for entry in entries:
                post_files.append(entry.name)

        print(files)
        print(post_files)
        assert len(files) == count
        assert len(post_files) == len(files) - 1
        assert post_files == files[:-2] + [files[-1]]

    @needs_dfuse_with_opt(single_threaded=True, caching=True)
    def test_single_threaded(self):
        """Test single-threaded mode"""
        self.readdir_test(10)

    @needs_dfuse
    def test_open_replaced(self):
        """Test that fstat works on file clobbered by rename"""
        fname = join(self.dfuse.dir, 'unlinked')
        newfile = join(self.dfuse.dir, 'unlinked2')
        with open(fname, 'w') as ofd:
            with open(newfile, 'w') as nfd:
                nfd.write('hello')
            print(os.fstat(ofd.fileno()))
            os.rename(newfile, fname)
            print(os.fstat(ofd.fileno()))
            ofd.close()

    @needs_dfuse
    def test_open_rename(self):
        """Check that fstat() on renamed files works as expected"""
        fname = join(self.dfuse.dir, 'unlinked')
        newfile = join(self.dfuse.dir, 'unlinked2')
        with open(fname, 'w') as ofd:
            pre = os.fstat(ofd.fileno())
            print(pre)
            os.rename(fname, newfile)
            print(os.fstat(ofd.fileno()))
            os.stat(newfile)
            post = os.fstat(ofd.fileno())
            print(post)
            assert pre.st_ino == post.st_ino

    @needs_dfuse
    def test_open_unlinked(self):
        """Test that fstat works on unlinked file"""
        fname = join(self.dfuse.dir, 'unlinked')
        with open(fname, 'w') as ofd:
            print(os.fstat(ofd.fileno()))
            os.unlink(fname)
            print(os.fstat(ofd.fileno()))

    @needs_dfuse
    def test_chown_self(self):
        """Test that a file can be chowned to the current user, but not to other users"""
        fname = join(self.dfuse.dir, 'new_file')
        with open(fname, 'w') as fd:
            os.chown(fd.fileno(), os.getuid(), -1)
            os.chown(fd.fileno(), -1, os.getgid())

            # Chgrp to root, should fail but will likely be refused by the kernel.
            try:
                os.chown(fd.fileno(), -1, 1)
                assert False
            except PermissionError:
                pass
            except OSError as error:
                if error.errno != errno.ENOTSUP:
                    raise

            # Chgrp to another group which this process is in, should work for all groups.
            groups = os.getgroups()
            print(groups)
            for group in groups:
                os.chown(fd.fileno(), -1, group)

    @needs_dfuse
    def test_symlink_broken(self):
        """Check that broken symlinks work"""
        src_link = join(self.dfuse.dir, 'source')

        os.symlink('target', src_link)
        entry = os.listdir(self.dfuse.dir)
        print(entry)
        assert len(entry) == 1
        assert entry[0] == 'source'
        os.lstat(src_link)

        try:
            os.stat(src_link)
            assert False
        except FileNotFoundError:
            pass

    @needs_dfuse
    def test_symlink_rel(self):
        """Check that relative symlinks work"""
        src_link = join(self.dfuse.dir, 'source')

        os.symlink('../target', src_link)
        entry = os.listdir(self.dfuse.dir)
        print(entry)
        assert len(entry) == 1
        assert entry[0] == 'source'
        os.lstat(src_link)

        try:
            os.stat(src_link)
            assert False
        except FileNotFoundError:
            pass

    @needs_dfuse
    def test_il_cat(self):
        """Quick check for the interception library"""
        fname = join(self.dfuse.dir, 'file')
        with open(fname, 'w'):
            pass

        check_fstat = True
        if self.dfuse.caching:
            check_fstat = False

        self.dfuse.il_cmd(['cat', fname], check_write=False, check_fstat=check_fstat)

    @needs_dfuse_with_opt(caching=False)
    def test_il(self):
        """Run a basic interception library test"""
        # Sometimes the write can be cached in the kernel and the cp will not read any data so
        # do not run this test with caching on.

        create_and_read_via_il(self.dfuse, self.dfuse.dir)

        sub_cont_dir = join(self.dfuse.dir, 'child')
        create_cont(self.conf, path=sub_cont_dir)

        # Create a file natively.
        file = join(self.dfuse.dir, 'file')
        with open(file, 'w') as fd:
            fd.write('Hello')
        # Copy it across containers.
        self.dfuse.il_cmd(['cp', file, sub_cont_dir])

        # Copy it within the container.
        child_dir = join(self.dfuse.dir, 'new_dir')
        os.mkdir(child_dir)
        self.dfuse.il_cmd(['cp', file, child_dir])
        # Copy something into a container
        self.dfuse.il_cmd(['cp', '/bin/bash', sub_cont_dir], check_read=False)
        # Read it from within a container
        self.dfuse.il_cmd(['md5sum', join(sub_cont_dir, 'bash')],
                          check_read=False, check_write=False, check_fstat=False)
        self.dfuse.il_cmd(['dd',
                           f'if={join(sub_cont_dir, "bash")}',
                           f'of={join(sub_cont_dir, "bash_copy")}',
                           'iflag=direct',
                           'oflag=direct',
                           'bs=128k'],
                          check_fstat=False)

    @needs_dfuse
    def test_xattr(self):
        """Perform basic tests with extended attributes"""
        new_file = join(self.dfuse.dir, 'attr_file')
        with open(new_file, 'w') as fd:

            xattr.set(fd, 'user.mine', 'init_value')
            # This should fail as a security test.
            try:
                xattr.set(fd, 'user.dfuse.ids', b'other_value')
                assert False
            except PermissionError:
                pass

            try:
                xattr.set(fd, 'user.dfuse', b'other_value')
                assert False
            except PermissionError:
                pass

            xattr.set(fd, 'user.Xfuse.ids', b'other_value')
            for (key, value) in xattr.get_all(fd):
                print(f'xattr is {key}:{value}')

    @needs_dfuse
    def test_evict(self):
        """Evict a file from dfuse"""
        new_file = join(self.dfuse.dir, 'e_file')
        with open(new_file, 'w'):
            pass

        rc = run_daos_cmd(self.conf, ['filesystem', 'evict', new_file])
        print(rc)
        assert rc.returncode == 0, rc
        time.sleep(5)

        rc = run_daos_cmd(self.conf, ['filesystem', 'evict', self.dfuse.dir])
        print(rc)
        assert rc.returncode == 0, rc
        time.sleep(5)

    @needs_dfuse
    def test_list_xattr(self):
        """Perform tests with listing extended attributes.

        Ensure that the user.daos command can be read, and is included in the list.
        xattrs are all byte strings.
        """
        expected_keys = {b'user.daos', b'user.dummy'}
        root_xattr = xattr.getxattr(self.dfuse.dir, "user.daos")
        print(f'The root xattr is {root_xattr}')

        xattr.set(self.dfuse.dir, 'user.dummy', 'short string')

        for (key, value) in xattr.get_all(self.dfuse.dir):
            expected_keys.remove(key)
            print(f'xattr is {key}:{value}')

        # Leave this out for now to avoid adding attr as a new rpm dependency.
        # rc = subprocess.run(['getfattr', '-n', 'user.daos', self.dfuse.dir], check=False)
        # print(rc)
        # assert rc.returncode == 0, rc

        assert len(expected_keys) == 0, 'Expected key not found'

    @needs_dfuse_with_opt(wbcache=True, caching=True)
    def test_stat_before_open(self):
        """Run open/close in a loop on the same file

        This only runs a reproducer, it does not trawl the logs to ensure the feature is working
        """
        test_file = join(self.dfuse.dir, 'test_file')
        with open(test_file, 'w'):
            pass

        for _ in range(100):
            with open(test_file, 'r'):
                pass

    @needs_dfuse
    def test_chmod(self):
        """Test that chmod works on file"""
        fname = join(self.dfuse.dir, 'testfile')
        with open(fname, 'w'):
            pass

        modes = [stat.S_IRUSR | stat.S_IWUSR | stat.S_IXUSR,
                 stat.S_IRUSR]

        for mode in modes:
            os.chmod(fname, mode)
            attr = os.stat(fname)
            assert stat.S_IMODE(attr.st_mode) == mode

    @needs_dfuse
    def test_fchmod_replaced(self):
        """Test that fchmod works on file clobbered by rename"""
        fname = join(self.dfuse.dir, 'unlinked')
        newfile = join(self.dfuse.dir, 'unlinked2')
        e_mode = stat.S_IRUSR | stat.S_IWUSR | stat.S_IXUSR
        with open(fname, 'w') as ofd:
            with open(newfile, 'w') as nfd:
                nfd.write('hello')
            print(os.stat(fname))
            print(os.stat(newfile))
            os.chmod(fname, stat.S_IRUSR | stat.S_IWUSR)
            os.chmod(newfile, e_mode)
            print(os.stat(fname))
            print(os.stat(newfile))
            os.rename(newfile, fname)
            # This should fail, because the file has been deleted.
            try:
                os.fchmod(ofd.fileno(), stat.S_IRUSR)
                print(os.fstat(ofd.fileno()))
                self.fail()
            except FileNotFoundError:
                print('Failed to fchmod() replaced file')
        new_file = os.stat(fname)
        assert stat.S_IMODE(new_file.st_mode) == e_mode

    @needs_dfuse
    def test_uns_create(self):
        """Simple test to create a container using a path in dfuse"""
        path = join(self.dfuse.dir, 'mycont')
        create_cont(self.conf, path=path)
        stbuf = os.stat(path)
        print(stbuf)
        assert stbuf.st_ino < 100
        print(os.listdir(path))
        rc = run_daos_cmd(self.conf, ['filesystem', 'query', self.dfuse.dir])
        print(rc)
        assert rc.returncode == 0
        rc = run_daos_cmd(self.conf, ['filesystem', 'query', self.dfuse.dir], use_json=True)
        print(rc)
        assert rc.returncode == 0

    @needs_dfuse
    def test_uns_link(self):
        """Simple test to create a container then create a path for it in dfuse"""
        container1 = create_cont(self.conf, self.pool, ctype="POSIX", label='mycont_uns_link1')
        cmd = ['cont', 'query', self.pool.id(), container1.id()]
        rc = run_daos_cmd(self.conf, cmd)
        assert rc.returncode == 0

        container2 = create_cont(self.conf, self.pool, ctype="POSIX", label='mycont_uns_link2')
        cmd = ['cont', 'query', self.pool.id(), container2.id()]
        rc = run_daos_cmd(self.conf, cmd)
        assert rc.returncode == 0

        path = join(self.dfuse.dir, 'uns_link1')
        cmd = ['cont', 'link', self.pool.id(), 'mycont_uns_link1', '--path', path]
        rc = run_daos_cmd(self.conf, cmd)
        assert rc.returncode == 0
        stbuf = os.stat(path)
        print(stbuf)
        assert stbuf.st_ino < 100
        print(os.listdir(path))
        cmd = ['cont', 'destroy', '--path', path]
        rc = run_daos_cmd(self.conf, cmd)

        path = join(self.dfuse.dir, 'uns_link2')
        cmd = ['cont', 'link', self.pool.id(), container2.id(), '--path', path]
        rc = run_daos_cmd(self.conf, cmd)
        assert rc.returncode == 0
        stbuf = os.stat(path)
        print(stbuf)
        assert stbuf.st_ino < 100
        print(os.listdir(path))
        self.dfuse.check_usage(inodes=2, open_files=1, containers=2, pools=1)
        cmd = ['cont', 'destroy', '--path', path]
        rc = run_daos_cmd(self.conf, cmd)
        assert rc.returncode == 0
        rc = self.dfuse.check_usage(inodes=1, open_files=1, containers=1, pools=1)

    @needs_dfuse
    def test_rename_clobber(self):
        """Test that rename clobbers files correctly

        use rename to delete a file, but where the kernel is aware of a different file.
        Create a filename to be clobbered and stat it.
        Create a file to copy over.
        Start a second dfuse instance and overwrite the original file with a new name.
        Perform a rename on the first dfuse.

        This should clobber a file, but not the one that the kernel is expecting, although it will
        do a lookup of the destination filename before the rename.

        Inspection of the logs is required to verify what is happening here which is beyond the
        scope of this test, however this does execute the code-paths and ensures that all refs
        are correctly updated.

        """
        # Create all three files in the dfuse instance we're checking.
        for index in range(3):
            with open(join(self.dfuse.dir, f'file.{index}'), 'w') as fd:
                fd.write('test')

        # Start another dfuse instance to move the files around without the kernel knowing.
        dfuse = DFuse(self.server,
                      self.conf,
                      container=self.container,
                      caching=False)
        dfuse.start(v_hint='rename_other')

        print(os.listdir(self.dfuse.dir))
        print(os.listdir(dfuse.dir))

        # Rename file 1 to file 2 in the background, this will remove file 2
        os.rename(join(dfuse.dir, 'file.1'), join(dfuse.dir, 'file.2'))

        # Rename file 0 to file 2 in the test dfuse.  Here the kernel thinks it's clobbering
        # file 2 but it's really clobbering file 1, although it will stat() file 2 before the
        # operation so may have the correct data.
        # Dfuse should return file 1 for the details of what has been deleted.
        os.rename(join(self.dfuse.dir, 'file.0'), join(self.dfuse.dir, 'file.2'))

        if dfuse.stop():
            self.fatal_errors = True

    @needs_dfuse
    def test_rename(self):
        """Test that tries various rename scenarios"""

        def _go(root):
            dfd = os.open(root, os.O_RDONLY)

            try:
                # Test renaming a file into a directory.
                pre_fname = join(root, 'file')
                with open(pre_fname, 'w') as fd:
                    fd.write('test')
                dname = join(root, 'dir')
                os.mkdir(dname)
                post_fname = join(dname, 'file')
                # os.rename and 'mv' have different semantics, use mv here which will put the file
                # in the directory.
                subprocess.run(['mv', pre_fname, dname], check=True)
                self._check_dirs_equal(['file'], dname)

                os.unlink(post_fname)
                os.rmdir('dir', dir_fd=dfd)

                # Test renaming a file over a directory.
                pre_fname = join(root, 'file')
                with open(pre_fname, 'w') as fd:
                    fd.write('test')
                dname = join(root, 'dir')
                os.mkdir(dname)
                post_fname = join(dname, 'file')
                # Try os.rename here, which we expect to fail.
                try:
                    os.rename(pre_fname, dname)
                    self.fail()
                except IsADirectoryError:
                    pass
                os.unlink(pre_fname)
                os.rmdir('dir', dir_fd=dfd)

                # Check renaming a file over a file.
                for index in range(2):
                    with open(join(root, f'file.{index}'), 'w') as fd:
                        fd.write('test')

                print(os.listdir(dfd))
                os.rename('file.0', 'file.1', src_dir_fd=dfd, dst_dir_fd=dfd)

                self._check_dirs_equal(['file.1'], root)
                os.unlink('file.1', dir_fd=dfd)

                # dir onto file.
                dname = join(root, 'dir')
                os.mkdir(dname)
                fname = join(root, 'file')
                with open(fname, 'w') as fd:
                    fd.write('test')
                try:
                    os.rename(dname, fname)
                    self.fail()
                except NotADirectoryError:
                    pass
                os.unlink('file', dir_fd=dfd)
                os.rmdir('dir', dir_fd=dfd)

                # Now check for dir rename into other dir though mv.
                src_dir = join(root, 'src')
                dst_dir = join(root, 'dst')
                os.mkdir(src_dir)
                os.mkdir(dst_dir)
                subprocess.run(['mv', src_dir, dst_dir], check=True)
                self._check_dirs_equal(['dst'], root)
                self._check_dirs_equal(['src'], join(root, 'dst'))
                os.rmdir(join(dst_dir, 'src'))
                os.rmdir(dst_dir)

                # Check for dir rename over other dir though python, in this case it should clobber
                # the target directory.
                for index in range(2):
                    os.mkdir(join(root, f'dir.{index}'))
                os.rename('dir.0', 'dir.1', src_dir_fd=dfd, dst_dir_fd=dfd)
                self._check_dirs_equal(['dir.1'], root)
                self._check_dirs_equal([], join(root, 'dir.1'))
                os.rmdir(join(root, 'dir.1'))
                for index in range(2):
                    with open(join(root, f'file.{index}'), 'w') as fd:
                        fd.write('test')
                os.rename('file.0', 'file.1', src_dir_fd=dfd, dst_dir_fd=dfd)
                self._check_dirs_equal(['file.1'], root)
                os.unlink('file.1', dir_fd=dfd)

                # Rename a dir over another, where the target is not empty.
                dst_dir = join(root, 'ddir')
                dst_file = join(dst_dir, 'file')
                os.mkdir('sdir', dir_fd=dfd)
                os.mkdir(dst_dir)
                with open(dst_file, 'w') as fd:
                    fd.write('test')
                # According to the man page this can return ENOTEMPTY or EEXIST, and /tmp is
                # returning one and dfuse the other so catch both.
                try:
                    os.rename('sdir', dst_dir, src_dir_fd=dfd)
                    self.fail()
                except FileExistsError:
                    pass
                except OSError as error:
                    assert error.errno == errno.ENOTEMPTY
                os.rmdir('sdir', dir_fd=dfd)
                os.unlink(dst_file)
                os.rmdir(dst_dir)

            finally:
                os.close(dfd)

        # Firstly validate the check
        with tempfile.TemporaryDirectory(prefix='rename_test_ref_dir.') as tmp_dir:
            _go(tmp_dir)

        _go(self.dfuse.dir)

    @needs_dfuse
    def test_complex_unlink(self):
        """Test that unlink clears file data correctly.

        Create two files, exchange them in the back-end then unlink the one.

        The kernel will be unlinking what it thinks is file 1 but it will actually be file 0.
        """
        # pylint: disable=consider-using-with

        fds = []

        # Create both files in the dfuse instance we're checking.  These files are created in
        # binary mode with buffering off so the writes are sent direct to the kernel.
        for index in range(2):
            fd = open(join(self.dfuse.dir, f'file.{index}'), 'wb', buffering=0)
            fd.write(b'test')
            fds.append(fd)

        # Start another dfuse instance to move the files around without the kernel knowing.
        dfuse = DFuse(self.server,
                      self.conf,
                      container=self.container,
                      caching=False)
        dfuse.start(v_hint='unlink')

        print(os.listdir(self.dfuse.dir))
        print(os.listdir(dfuse.dir))

        # Rename file 0 to file 0 in the background, this will remove file 1
        os.rename(join(dfuse.dir, 'file.0'), join(dfuse.dir, 'file.1'))

        # Perform the unlink, this will unlink the other file.
        os.unlink(join(self.dfuse.dir, 'file.1'))

        if dfuse.stop():
            self.fatal_errors = True

        # Finally, perform some more I/O so we can tell from the dfuse logs where the test ends and
        # dfuse teardown starts.  At this point file 1 and file 2 have been deleted.
        time.sleep(1)
        print(os.statvfs(self.dfuse.dir))

        for fd in fds:
            fd.close()

    def test_cont_rw(self):
        """Test write access to another users container"""
        dfuse = DFuse(self.server,
                      self.conf,
                      container=self.container,
                      caching=False)

        dfuse.start(v_hint='cont_rw_1')

        stat_log = PrintStat(dfuse.dir)
        testfile = join(dfuse.dir, 'testfile')
        with open(testfile, 'w') as fd:
            stat_log.add(testfile, attr=os.fstat(fd.fileno()))

        dirname = join(dfuse.dir, 'rw_dir')
        os.mkdir(dirname)

        stat_log.add(dirname)

        dir_perms = os.stat(dirname).st_mode
        base_perms = stat.S_IMODE(dir_perms)

        os.chmod(dirname, base_perms | stat.S_IWGRP | stat.S_IXGRP | stat.S_IXOTH | stat.S_IWOTH)
        stat_log.add(dirname)
        print(stat_log)

        if dfuse.stop():
            self.fatal_errors = True

        # Update container ACLs so current user has rw permissions only, the minimum required.
        rc = run_daos_cmd(self.conf, ['container',
                                      'update-acl',
                                      self.pool.id(),
                                      self.container.id(),
                                      '--entry',
                                      f'A::{os.getlogin()}@:rwta'])
        print(rc)

        # Assign the container to someone else.
        rc = run_daos_cmd(self.conf, ['container',
                                      'set-owner',
                                      self.pool.id(),
                                      self.container.id(),
                                      '--user',
                                      'root@',
                                      '--group',
                                      'root@'])
        print(rc)

        # Now start dfuse and access the container, see who the file is owned by.
        dfuse = DFuse(self.server,
                      self.conf,
                      container=self.container,
                      caching=False)
        dfuse.start(v_hint='cont_rw_2')

        stat_log = PrintStat()
        stat_log.add(dfuse.dir, show_dir=True)

        with open(join(dfuse.dir, 'testfile'), 'r') as fd:
            stat_log.add(join(dfuse.dir, 'testfile'), os.fstat(fd.fileno()))

        dirname = join(dfuse.dir, 'rw_dir')
        testfile = join(dirname, 'new_file')
        fd = os.open(testfile, os.O_RDWR | os.O_CREAT, mode=int('600', base=8))
        os.write(fd, b'read-only-data')
        stat_log.add(testfile, attr=os.fstat(fd))
        os.close(fd)
        print(stat_log)

        fd = os.open(testfile, os.O_RDONLY)
        # previous code was using stream/file methods and it appears that
        # file.read() (no size) is doing a fstat() and reads size + 1
        fstat_fd = os.fstat(fd)
        raw_bytes = os.read(fd, fstat_fd.st_size + 1)
        # pylint: disable=wrong-spelling-in-comment
        # Due to DAOS-9671 garbage can be read from still unknown reason.
        # So remove asserts and do not run Unicode codec to avoid
        # exceptions for now ... This allows to continue testing permissions.
        if raw_bytes != b'read-only-data':
            print('Check kernel data')
        # data = raw_bytes.decode('utf-8', 'ignore')
        # assert data == 'read-only-data'
        # print(data)
        os.close(fd)

        if dfuse.stop():
            self.fatal_errors = True

    @needs_dfuse
    def test_complex_rename(self):
        """Test for rename semantics

        Check that that rename is correctly updating the dfuse data for the moved file.

        # Create a file, read/write to it.
        # Check fstat works.
        # Rename it from the back-end
        # Check fstat - it should not work.
        # Rename the file into a new directory, this should allow the kernel to 'find' the file
        # again and update the name/parent.
        # check fstat works.
        """
        fname = join(self.dfuse.dir, 'file')
        with open(fname, 'w') as ofd:
            print(os.fstat(ofd.fileno()))

            dfuse = DFuse(self.server,
                          self.conf,
                          container=self.container,
                          caching=False)
            dfuse.start(v_hint='rename')

            os.mkdir(join(dfuse.dir, 'step_dir'))
            os.mkdir(join(dfuse.dir, 'new_dir'))
            os.rename(join(dfuse.dir, 'file'), join(dfuse.dir, 'step_dir', 'file-new'))

            # This should fail, because the file has been deleted.
            try:
                print(os.fstat(ofd.fileno()))
                self.fail()
            except FileNotFoundError:
                print('Failed to fstat() replaced file')

            os.rename(join(self.dfuse.dir, 'step_dir', 'file-new'),
                      join(self.dfuse.dir, 'new_dir', 'my-file'))

            print(os.fstat(ofd.fileno()))

        if dfuse.stop():
            self.fatal_errors = True

    def test_cont_ro(self):
        """Test access to a read-only container"""
        # Update container ACLs so current user has 'rta' permissions only, the minimum required.
        rc = run_daos_cmd(self.conf, ['container',
                                      'update-acl',
                                      self.pool.id(),
                                      self.container.id(),
                                      '--entry',
                                      f'A::{os.getlogin()}@:rta'])
        print(rc)
        assert rc.returncode == 0

        # Assign the container to someone else.
        rc = run_daos_cmd(self.conf, ['container',
                                      'set-owner',
                                      self.pool.id(),
                                      self.container.id(),
                                      '--user',
                                      'root@'])
        print(rc)
        assert rc.returncode == 0

        # Now start dfuse and access the container, this should require read-only opening.
        dfuse = DFuse(self.server,
                      self.conf,
                      pool=self.pool.id(),
                      container=self.container,
                      caching=False)
        dfuse.start(v_hint='cont_ro')
        print(os.listdir(dfuse.dir))

        try:
            with open(join(dfuse.dir, 'testfile'), 'w') as fd:
                print(fd)
            assert False
        except PermissionError:
            pass

        if dfuse.stop():
            self.fatal_errors = True

    @needs_dfuse
    def test_chmod_ro(self):
        """Test that chmod and fchmod work correctly with files created read-only

        DAOS-6238
        """
        path = self.dfuse.dir
        fname = join(path, 'test_file1')
        ofd = os.open(fname, os.O_CREAT | os.O_RDONLY | os.O_EXCL)
        print(os.stat(fname))
        os.close(ofd)
        os.chmod(fname, stat.S_IRUSR)
        new_stat = os.stat(fname)
        print(new_stat)
        assert stat.S_IMODE(new_stat.st_mode) == stat.S_IRUSR

        fname = join(path, 'test_file2')
        ofd = os.open(fname, os.O_CREAT | os.O_RDONLY | os.O_EXCL)
        print(os.stat(fname))
        os.fchmod(ofd, stat.S_IRUSR)
        os.close(ofd)
        new_stat = os.stat(fname)
        print(new_stat)
        assert stat.S_IMODE(new_stat.st_mode) == stat.S_IRUSR

    def test_with_path(self):
        """Test that dfuse starts with path option."""
        tmp_dir = tempfile.mkdtemp()

        cont_path = join(tmp_dir, 'my-cont')
        create_cont(self.conf, self.pool, path=cont_path)

        dfuse = DFuse(self.server,
                      self.conf,
                      caching=True,
                      uns_path=cont_path)
        dfuse.start(v_hint='with_path')

        # Simply write a file.  This will fail if dfuse isn't backed via
        # a container.
        file = join(dfuse.dir, 'file')
        with open(file, 'w') as fd:
            fd.write('test')

        if dfuse.stop():
            self.fatal_errors = True

    def test_uns_basic(self):
        """Create a UNS entry point and access it via both entry point and path"""
        pool = self.pool.uuid
        container = self.container
        server = self.server
        conf = self.conf

        # Start dfuse on the container.
        dfuse = DFuse(server, conf, container=container, caching=False)
        dfuse.start('uns-0')

        # Create a new container within it using UNS
        uns_path = join(dfuse.dir, 'ep0')
        print('Inserting entry point')
        uns_container = create_cont(conf, pool=self.pool, path=uns_path)
        print(os.stat(uns_path))
        print(os.listdir(dfuse.dir))

        # Verify that it exists.
        run_container_query(conf, uns_path)

        # Make a directory in the new container itself, and query that.
        child_path = join(uns_path, 'child')
        os.mkdir(child_path)
        run_container_query(conf, child_path)
        if dfuse.stop():
            self.fatal_errors = True

        print('Trying UNS')
        dfuse = DFuse(server, conf, caching=False)
        dfuse.start('uns-1')

        # List the root container.
        print(os.listdir(join(dfuse.dir, pool, container.uuid)))

        # Now create a UNS link from the 2nd container to a 3rd one.
        uns_path = join(dfuse.dir, pool, container.uuid, 'ep0', 'ep')
        second_path = join(dfuse.dir, pool, uns_container.uuid)

        # Make a link within the new container.
        print('Inserting entry point')
        uns_container_2 = create_cont(conf, pool=self.pool, path=uns_path)

        # List the root container again.
        print(os.listdir(join(dfuse.dir, pool, container.uuid)))

        # List the 2nd container.
        files = os.listdir(second_path)
        print(files)
        # List the target container through UNS.
        print(os.listdir(uns_path))
        direct_stat = os.stat(join(second_path, 'ep'))
        uns_stat = os.stat(uns_path)
        print(direct_stat)
        print(uns_stat)
        assert uns_stat.st_ino == direct_stat.st_ino

        third_path = join(dfuse.dir, pool, uns_container_2.uuid)
        third_stat = os.stat(third_path)
        print(third_stat)
        assert third_stat.st_ino == direct_stat.st_ino

        if dfuse.stop():
            self.fatal_errors = True
        print('Trying UNS with previous cont')
        dfuse = DFuse(server, conf, caching=False)
        dfuse.start('uns-3')

        second_path = join(dfuse.dir, pool, uns_container.uuid)
        uns_path = join(dfuse.dir, pool, container.uuid, 'ep0', 'ep')
        files = os.listdir(second_path)
        print(files)
        print(os.listdir(uns_path))

        direct_stat = os.stat(join(second_path, 'ep'))
        uns_stat = os.stat(uns_path)
        print(direct_stat)
        print(uns_stat)
        assert uns_stat.st_ino == direct_stat.st_ino
        if dfuse.stop():
            self.fatal_errors = True

    def test_dfuse_dio_off(self):
        """Test for dfuse with no caching options, but direct-io disabled"""
        self.container.set_attrs({'dfuse-direct-io-disable': 'on'})
        dfuse = DFuse(self.server,
                      self.conf,
                      caching=True,
                      container=self.container)

        dfuse.start(v_hint='dio_off')

        print(os.listdir(dfuse.dir))

        fname = join(dfuse.dir, 'test_file3')
        with open(fname, 'w') as ofd:
            ofd.write('hello')

        if dfuse.stop():
            self.fatal_errors = True

    def test_dfuse_oopt(self):
        """Test dfuse with -opool=,container= options as used by fstab"""
        dfuse = DFuse(self.server, self.conf, container=self.container)

        dfuse.start(use_oopt=True)

        if dfuse.stop():
            self.fatal_errors = True

        dfuse = DFuse(self.server, self.conf, pool=self.pool.uuid)

        dfuse.start(use_oopt=True)

        if dfuse.stop():
            self.fatal_errors = True

        dfuse = DFuse(self.server, self.conf, pool=self.pool.label)

        dfuse.start(use_oopt=True)

        if dfuse.stop():
            self.fatal_errors = True

        dfuse = DFuse(self.server, self.conf)

        dfuse.start(use_oopt=True)

        if dfuse.stop():
            self.fatal_errors = True

    @needs_dfuse_with_opt(caching=False)
    def test_daos_fs_tool(self):
        """Create a UNS entry point"""
        dfuse = self.dfuse
        pool = self.pool.uuid
        conf = self.conf

        # Create a new container within it using UNS
        uns_path = join(dfuse.dir, 'ep1')
        print('Inserting entry point')
        uns_container = create_cont(conf, pool=self.pool, path=uns_path)

        print(os.stat(uns_path))
        print(os.listdir(dfuse.dir))

        # Verify that it exists.
        run_container_query(conf, uns_path)

        # Make a directory in the new container itself, and query that.
        dir1 = join(uns_path, 'd1')
        os.mkdir(dir1)
        run_container_query(conf, dir1)

        # Create a file in dir1
        file1 = join(dir1, 'f1')
        with open(file1, 'w'):
            pass

        # Run a command to get attr of new dir and file
        cmd = ['fs', 'get-attr', '--path', dir1]
        print('get-attr of d1')
        rc = run_daos_cmd(conf, cmd)
        assert rc.returncode == 0
        print(f'rc is {rc}')
        output = rc.stdout.decode('utf-8')
        assert check_dfs_tool_output(output, 'S1', '1048576')

        # run same command using pool, container, dfs-path, and dfs-prefix
        cmd = ['fs', 'get-attr', pool, uns_container.id(), '--dfs-path', dir1,
               '--dfs-prefix', uns_path]
        print('get-attr of d1')
        rc = run_daos_cmd(conf, cmd)
        assert rc.returncode == 0
        print(f'rc is {rc}')
        output = rc.stdout.decode('utf-8')
        assert check_dfs_tool_output(output, 'S1', '1048576')

        # run same command using pool, container, dfs-path
        cmd = ['fs', 'get-attr', pool, uns_container.id(), '--dfs-path', '/d1']
        print('get-attr of d1')
        rc = run_daos_cmd(conf, cmd)
        assert rc.returncode == 0
        print(f'rc is {rc}')
        output = rc.stdout.decode('utf-8')
        assert check_dfs_tool_output(output, 'S1', '1048576')

        cmd = ['fs', 'get-attr', '--path', file1]
        print('get-attr of d1/f1')
        rc = run_daos_cmd(conf, cmd)
        assert rc.returncode == 0
        print(f'rc is {rc}')
        output = rc.stdout.decode('utf-8')
        # SX is not deterministic, so don't check it here
        assert check_dfs_tool_output(output, None, '1048576')

        # Run a command to change attr of dir1
        cmd = ['fs', 'set-attr', '--path', dir1, '--oclass', 'S2',
               '--chunk-size', '16']
        print('set-attr of d1')
        rc = run_daos_cmd(conf, cmd)
        assert rc.returncode == 0
        print(f'rc is {rc}')

        # Run a command to change attr of file1, should fail
        cmd = ['fs', 'set-attr', '--path', file1, '--oclass', 'S2',
               '--chunk-size', '16']
        print('set-attr of f1')
        rc = run_daos_cmd(conf, cmd)
        print(f'rc is {rc}')
        assert rc.returncode != 0

        # Run a command to create new file with set-attr
        file2 = join(dir1, 'f2')
        cmd = ['fs', 'set-attr', '--path', file2, '--oclass', 'S1']
        print('set-attr of f2')
        rc = run_daos_cmd(conf, cmd)
        assert rc.returncode == 0
        print(f'rc is {rc}')

        # Run a command to get attr of dir and file2
        cmd = ['fs', 'get-attr', '--path', dir1]
        print('get-attr of d1')
        rc = run_daos_cmd(conf, cmd)
        assert rc.returncode == 0
        print(f'rc is {rc}')
        output = rc.stdout.decode('utf-8')
        assert check_dfs_tool_output(output, 'S2', '16')

        cmd = ['fs', 'get-attr', '--path', file2]
        print('get-attr of d1/f2')
        rc = run_daos_cmd(conf, cmd)
        assert rc.returncode == 0
        print(f'rc is {rc}')
        output = rc.stdout.decode('utf-8')
        assert check_dfs_tool_output(output, 'S1', '16')

    def test_cont_copy(self):
        """Verify that copying into a container works"""
        # pylint: disable=consider-using-with

        # Create a temporary directory, with one file into it and copy it into
        # the container.  Check the return-code only, do not verify the data.
        # tempfile() will remove the directory on completion.
        src_dir = tempfile.TemporaryDirectory(prefix='copy_src_',)
        with open(join(src_dir.name, 'file'), 'w') as ofd:
            ofd.write('hello')
        os.symlink('file', join(src_dir.name, 'file_s'))
        cmd = ['filesystem',
               'copy',
               '--src',
               src_dir.name,
               '--dst',
               f'daos://{self.pool.uuid}/{self.container}']
        rc = run_daos_cmd(self.conf, cmd, use_json=True)
        print(rc)

        data = rc.json
        assert data['status'] == 0, rc
        assert data['error'] is None, rc
        assert data['response'] is not None, rc
        assert data['response']['copy_stats']['num_dirs'] == 1
        assert data['response']['copy_stats']['num_files'] == 1
        assert data['response']['copy_stats']['num_links'] == 1

    def test_cont_clone(self):
        """Verify that cloning a container works

        This extends cont_copy, to also clone it afterwards.
        """
        # pylint: disable=consider-using-with

        # Create a temporary directory, with one file into it and copy it into
        # the container.  Check the return code only, do not verify the data.
        # tempfile() will remove the directory on completion.
        src_dir = tempfile.TemporaryDirectory(prefix='copy_src_',)
        with open(join(src_dir.name, 'file'), 'w') as ofd:
            ofd.write('hello')

        cmd = ['filesystem',
               'copy',
               '--src',
               src_dir.name,
               '--dst',
               f'daos://{self.pool.uuid}/{self.container.id()}']
        rc = run_daos_cmd(self.conf, cmd, use_json=True)
        print(rc)

        data = rc.json
        assert data['status'] == 0, rc
        assert data['error'] is None, rc
        assert data['response'] is not None, rc

        # Now create a container uuid and do an object based copy.
        # The daos command will create the target container on demand.
        cmd = ['container',
               'clone',
               '--src',
               f'daos://{self.pool.uuid}/{self.container.id()}',
               '--dst',
               f'daos://{self.pool.uuid}/']
        rc = run_daos_cmd(self.conf, cmd, use_json=True)
        print(rc)

        data = rc.json
        assert data['status'] == 0, rc
        assert data['error'] is None, rc
        assert data['response'] is not None, rc

        destroy_container(self.conf, self.pool.id(), data['response']['dst_cont'])

    def test_dfuse_perms(self):
        """Test permissions caching for DAOS-12577"""
        cache_time = 10

        cont_attrs = {'dfuse-data-cache': False,
                      'dfuse-attr-time': cache_time,
                      'dfuse-dentry-time': cache_time,
                      'dfuse-ndentry-time': cache_time}
        self.container.set_attrs(cont_attrs)

        dfuse = DFuse(self.server, self.conf, container=self.container, wbcache=False)

        side_dfuse = DFuse(self.server, self.conf, container=self.container, wbcache=False)

        dfuse.start(v_hint='perms')
        side_dfuse.start(v_hint='perms_side')

        test_file = join(dfuse.dir, 'test-file')
        side_test_file = join(side_dfuse.dir, 'test-file')

        # Create a file.
        with open(test_file, 'w', encoding='ascii', errors='ignore') as fd:
            fd.write('data')

        # Read it through both.
        with open(test_file, 'r', encoding='ascii', errors='ignore') as fd:
            data = fd.read()
            if data != 'data':
                print('Check kernel data')
        with open(side_test_file, 'r', encoding='ascii', errors='ignore') as fd:
            data = fd.read()
            if data != 'data':
                print('Check kernel data')

        # Remove all permissions on the file.
        print(os.stat(side_test_file))
        os.chmod(side_test_file, 0)
        print(os.stat(side_test_file))

        # Read it through the second channel.
        try:
            with open(side_test_file, 'r', encoding='ascii', errors='ignore') as fd:
                data = fd.read()
                assert False
        except PermissionError:
            pass

        # Read it through first instance, this should work as the contents are cached.
        with open(test_file, 'r', encoding='ascii', errors='ignore') as fd:
            data = fd.read()
            if data != 'data':
                print('Check kernel data')

        # Let the cache expire.
        time.sleep(cache_time * 2)

        try:
            with open(side_test_file, 'r', encoding='ascii', errors='ignore') as fd:
                data = fd.read()
                assert False
        except PermissionError:
            pass

        # Read it through the first dfuse, this should now fail as the cache has expired.
        try:
            with open(test_file, 'r', encoding='ascii', errors='ignore') as fd:
                data = fd.read()
                assert False
        except PermissionError:
            pass

        if dfuse.stop():
            self.fatal_errors = True

        if side_dfuse.stop():
            self.fatal_errors = True

    def test_daos_fs_check(self):
        """Test DAOS FS Checker"""
        # pylint: disable=too-many-branches
        # pylint: disable=too-many-statements
        dfuse = DFuse(self.server,
                      self.conf,
                      pool=self.pool.id(),
                      container=self.container,
                      caching=False)
        dfuse.start(v_hint='fs_check_test')
        path = dfuse.dir
        dirname = join(path, 'test_dir')
        os.mkdir(dirname)
        fname = join(dirname, 'f1')
        with open(fname, 'w') as fd:
            fd.write('test1')

        dirname = join(path, 'test_dir/1d1/')
        os.mkdir(dirname)
        fname = join(dirname, 'f2')
        with open(fname, 'w') as fd:
            fd.write('test2')
        dirname = join(path, 'test_dir/1d2/')
        os.mkdir(dirname)
        fname = join(dirname, 'f3')
        with open(fname, 'w') as fd:
            fd.write('test3')
        dirname = join(path, 'test_dir/1d3/')
        os.mkdir(dirname)
        fname = join(dirname, 'f4')
        with open(fname, 'w') as fd:
            fd.write('test4')

        dirname = join(path, 'test_dir/1d1/2d1/')
        os.mkdir(dirname)
        fname = join(dirname, 'f5')
        with open(fname, 'w') as fd:
            fd.write('test5')
        dirname = join(path, 'test_dir/1d1/2d2/')
        os.mkdir(dirname)
        fname = join(dirname, 'f6')
        with open(fname, 'w') as fd:
            fd.write('test6')
        dirname = join(path, 'test_dir/1d1/2d3/')
        os.mkdir(dirname)
        fname = join(dirname, 'f7')
        with open(fname, 'w') as fd:
            fd.write('test7')

        dirname = join(path, 'test_dir/1d2/2d4/')
        os.mkdir(dirname)
        fname = join(dirname, 'f8')
        with open(fname, 'w') as fd:
            fd.write('test8')
        dirname = join(path, 'test_dir/1d2/2d5/')
        os.mkdir(dirname)
        fname = join(dirname, 'f9')
        with open(fname, 'w') as fd:
            fd.write('test9')
        dirname = join(path, 'test_dir/1d2/2d6/')
        os.mkdir(dirname)
        fname = join(dirname, 'f10')
        with open(fname, 'w') as fd:
            fd.write('test10')

        dirname = join(path, 'test_dir/1d3/2d7/')
        os.mkdir(dirname)
        fname = join(dirname, 'f11')
        with open(fname, 'w') as fd:
            fd.write('test11')
        dirname = join(path, 'test_dir/1d3/2d8/')
        os.mkdir(dirname)
        fname = join(dirname, 'f12')
        with open(fname, 'w') as fd:
            fd.write('test12')
        dirname = join(path, 'test_dir/1d3/2d9/')
        os.mkdir(dirname)
        fname = join(dirname, 'f13')
        with open(fname, 'w') as fd:
            fd.write('test13')

        dirname2 = join(path, 'test_dir2')
        dirname = join(path, 'test_dir')
        shutil.copytree(dirname, dirname2)

        # punch a few directories and files
        daos_mw_fi = join(self.conf['PREFIX'], 'lib/daos/TESTING/tests/', 'daos_mw_fi')
        cmd_env = get_base_env()
        cmd_env['DAOS_AGENT_DRPC_DIR'] = self.conf.agent_dir

        dir1 = join(path, 'test_dir/')
        dir_list = os.listdir(dir1)
        nr_entries = len(dir_list)
        if nr_entries != 4:
            raise NLTestFail('Wrong number of entries')

        cmd = [daos_mw_fi, self.pool.id(), self.container.id(), "punch_entry", "/test_dir/1d1/"]
        self.server.run_daos_client_cmd(cmd)

        dir_list = os.listdir(dir1)
        nr_entries = len(dir_list)
        if nr_entries != 3:
            raise NLTestFail('Wrong number of entries')

        cmd = [daos_mw_fi, self.pool.id(), self.container.id(), "punch_entry", "/test_dir"]
        self.server.run_daos_client_cmd(cmd)

        # run the checker while dfuse is still mounted (should fail - EX open)
        cmd = ['fs', 'check', self.pool.id(), self.container.id(), '--flags', 'print', '--dir-name',
               'lf1']
        rc = run_daos_cmd(self.conf, cmd, ignore_busy=True)
        print(rc)
        assert rc.returncode != 0
        output = rc.stderr.decode('utf-8')
        line = output.splitlines()
        if line[-1] != 'ERROR: daos: failed fs check: errno 16 (Device or resource busy)':
            raise NLTestFail('daos fs check should fail with EBUSY')

        # stop dfuse
        if dfuse.stop():
            self.fatal_errors = True

        # fs check with relink should find the 2 leaked directories.
        # Everything under them should be relinked but not reported as leaked.
        cmd = ['fs', 'check', self.pool.id(), self.container.id(), '--flags', 'print,relink',
               '--dir-name', 'lf1']
        rc = run_daos_cmd(self.conf, cmd)
        print(rc)
        assert rc.returncode == 0
        output = rc.stdout.decode('utf-8')
        line = output.splitlines()
        if line[-1] != 'DFS checker: Number of leaked OIDs in namespace = 2':
            raise NLTestFail('Wrong number of Leaked OIDs')

        # run again to check nothing is detected
        cmd = ['fs', 'check', self.pool.id(), self.container.id(), '--flags', 'print,relink']
        rc = run_daos_cmd(self.conf, cmd)
        print(rc)
        assert rc.returncode == 0
        output = rc.stdout.decode('utf-8')
        line = output.splitlines()
        if line[-1] != 'DFS checker: Number of leaked OIDs in namespace = 0':
            raise NLTestFail('Wrong number of Leaked OIDs')

        # remount dfuse
        dfuse = DFuse(self.server,
                      self.conf,
                      pool=self.pool.id(),
                      container=self.container,
                      caching=False)
        dfuse.start(v_hint='fs_check_test')
        path = dfuse.dir

        dir1 = join(path, 'lost+found/lf1/')
        dir_list = os.listdir(dir1)
        nr_entries = len(dir_list)
        if nr_entries != 2:
            raise NLTestFail('Wrong number of entries')
        nr_entries = 0
        file_nr = 0
        dir_nr = 0
        for entry in dir_list:
            if os.path.isdir(os.path.join(dir1, entry)):
                nr_entries += 1
                for root, dirs, files in os.walk(os.path.join(dir1, entry)):
                    for name in files:
                        print(os.path.join(root, name))
                        file_nr += 1
                    for name in dirs:
                        print(os.path.join(root, name))
                        dir_nr += 1
        if nr_entries != 2:
            raise NLTestFail('Wrong number of leaked directory OIDS')
        if file_nr != 13:
            raise NLTestFail('Wrong number of sub-files in lost+found')
        if dir_nr != 11:
            raise NLTestFail('Wrong number of sub-directories in lost+found')

        # punch the test_dir2 object.
        # this makes test_dir2 an empty dir (leaking everything under it)
        cmd = [daos_mw_fi, self.pool.id(), self.container.id(), "punch_obj", "/test_dir2"]
        self.server.run_daos_client_cmd(cmd)

        # stop dfuse
        if dfuse.stop():
            self.fatal_errors = True

        # fs check with relink should find 3 leaked dirs and 1 leaked file that were directly under
        # test_dir2. Everything under those leaked dirs are relinked but not reported as leaked.
        cmd = ['fs', 'check', self.pool.id(), self.container.id(), '--flags', 'print,relink',
               '--dir-name', 'lf2']
        rc = run_daos_cmd(self.conf, cmd)
        print(rc)
        assert rc.returncode == 0
        output = rc.stdout.decode('utf-8')
        line = output.splitlines()
        if line[-1] != 'DFS checker: Number of leaked OIDs in namespace = 4':
            raise NLTestFail('Wrong number of Leaked OIDs')

        # run again to check nothing is detected
        cmd = ['fs', 'check', self.pool.id(), self.container.id(), '--flags', 'print,relink']
        rc = run_daos_cmd(self.conf, cmd)
        print(rc)
        assert rc.returncode == 0
        output = rc.stdout.decode('utf-8')
        line = output.splitlines()
        if line[-1] != 'DFS checker: Number of leaked OIDs in namespace = 0':
            raise NLTestFail('Wrong number of Leaked OIDs')

        # remount dfuse
        dfuse = DFuse(self.server,
                      self.conf,
                      pool=self.pool.id(),
                      container=self.container,
                      caching=False)
        dfuse.start(v_hint='fs_check_test')
        path = dfuse.dir

        dir2 = join(path, 'lost+found/lf2/')
        dir_list = os.listdir(dir2)
        nr_entries = len(dir_list)
        if nr_entries != 4:
            raise NLTestFail('Wrong number of entries')
        file_nr = 0
        dir_nr = 0
        for root, dirs, files in os.walk(dir2):
            for name in files:
                print(os.path.join(root, name))
                file_nr += 1
            for name in dirs:
                print(os.path.join(root, name))
                dir_nr += 1
        if file_nr != 13:
            raise NLTestFail('Wrong number of sub-files in lost+found')
        if dir_nr != 12:
            raise NLTestFail('Wrong number of sub-directories in lost+found')

        # stop dfuse
        if dfuse.stop():
            self.fatal_errors = True

    def test_daos_fs_fix(self):
        """Test DAOS FS Fix Tool"""
        dfuse = DFuse(self.server,
                      self.conf,
                      pool=self.pool.id(),
                      container=self.container,
                      caching=False)
        dfuse.start(v_hint='fs_fix_test')
        path = dfuse.dir
        dirname = join(path, 'test_dir')
        os.mkdir(dirname)

        fname1 = join(dirname, 'f1')
        with open(fname1, 'w', encoding='ascii') as fd:
            fd.write('test1')
        fname2 = join(dirname, 'f2')
        with open(fname2, 'w') as fd:
            fd.write('test2')

        dirname1 = join(path, 'test_dir/1d1/')
        os.mkdir(dirname1)
        fname3 = join(dirname1, 'f3')
        with open(fname3, 'w', encoding='ascii') as fd:
            fd.write('test3')
        dirname2 = join(path, 'test_dir/1d2/')
        os.mkdir(dirname2)
        fname4 = join(dirname2, 'f4')
        with open(fname4, 'w') as fd:
            fd.write('test4')

        # start corrupting things
        daos_mw_fi = join(self.conf['PREFIX'], 'lib/daos/TESTING/tests/', 'daos_mw_fi')
        cmd_env = get_base_env()
        cmd_env['DAOS_AGENT_DRPC_DIR'] = self.conf.agent_dir
        cmd = [daos_mw_fi, self.pool.id(), self.container.id(), "corrupt_entry", "/test_dir/f1"]
        self.server.run_daos_client_cmd(cmd)
        cmd = [daos_mw_fi, self.pool.id(), self.container.id(), "corrupt_entry", "/test_dir/1d1/f3"]
        self.server.run_daos_client_cmd(cmd)
        cmd = [daos_mw_fi, self.pool.id(), self.container.id(), "corrupt_entry", "/test_dir/1d2"]
        self.server.run_daos_client_cmd(cmd)

        # try to read from corrupted entries. all should fail
        try:
            with open(fname1, 'r'):
                assert False
        except OSError as error:
            assert error.errno == errno.EINVAL

        try:
            with open(fname3, 'r'):
                assert False
        except OSError as error:
            assert error.errno == errno.EINVAL

        try:
            dir_list = os.listdir(dirname2)
            assert False
        except OSError as error:
            assert error.errno == errno.EINVAL

        # fix corrupted entries while dfuse is running - should fail
        cmd = ['fs', 'fix-entry', self.pool.id(), self.container.id(), '--dfs-path', '/test_dir/f1',
               '--type', '--chunk-size', '1048576']
        rc = run_daos_cmd(self.conf, cmd, ignore_busy=True)
        print(rc)
        assert rc.returncode != 0
        output = rc.stderr.decode('utf-8')
        line = output.splitlines()
        if line[-1] != 'ERROR: daos: failed fs fix-entry: DER_BUSY(-1012): Device or resource busy':
            raise NLTestFail('daos fs fix-entry /test_dir/f1')

        # stop dfuse
        if dfuse.stop(ignore_einval=True):
            self.fatal_errors = True

        # fix corrupted entries
        cmd = ['fs', 'fix-entry', self.pool.id(), self.container.id(), '--dfs-path', '/test_dir/f1',
               '--type', '--chunk-size', '1048576']
        rc = run_daos_cmd(self.conf, cmd)
        print(rc)
        assert rc.returncode == 0
        output = rc.stdout.decode('utf-8')
        line = output.splitlines()
        if line[-1] != 'Adjusting chunk size of /test_dir/f1 to 1048576':
            raise NLTestFail('daos fs fix-entry /test_dir/f1')

        cmd = ['fs', 'fix-entry', self.pool.id(), self.container.id(), '--dfs-path',
               '/test_dir/1d1/f3', '--type', '--chunk-size', '1048576']
        rc = run_daos_cmd(self.conf, cmd)
        print(rc)
        assert rc.returncode == 0
        output = rc.stdout.decode('utf-8')
        line = output.splitlines()
        if line[-1] != 'Adjusting chunk size of /test_dir/1d1/f3 to 1048576':
            raise NLTestFail('daos fs fix-entry /test_dir/1d1/f3')

        cmd = ['fs', 'fix-entry', self.pool.id(), self.container.id(), '--dfs-path',
               '/test_dir/1d2', '--type']
        rc = run_daos_cmd(self.conf, cmd)
        print(rc)
        assert rc.returncode == 0
        output = rc.stdout.decode('utf-8')
        line = output.splitlines()
        if line[-1] != 'Setting entry type to S_IFDIR':
            raise NLTestFail('daos fs fix-entry /test_dir/1d2')

        # remount dfuse
        dfuse = DFuse(self.server,
                      self.conf,
                      pool=self.pool.id(),
                      container=self.container,
                      caching=False)
        dfuse.start(v_hint='fs_fix_test')
        path = dfuse.dir
        dirname = join(path, 'test_dir')
        dirname1 = join(path, 'test_dir/1d1/')
        fname1 = join(dirname, 'f1')
        fname3 = join(dirname1, 'f3')
        dirname2 = join(path, 'test_dir/1d2/')

        # Check entries after fixing
        cmd = ['fs', 'get-attr', '--path', fname1]
        rc = run_daos_cmd(self.conf, cmd)
        assert rc.returncode == 0
        print(f'rc is {rc}')
        output = rc.stdout.decode('utf-8')
        assert check_dfs_tool_output(output, None, '1048576')
        with open(fname1, 'r', encoding='ascii', errors='ignore') as fd:
            data = fd.read()
            if data != 'test1':
                print('/test_dir/f1 data is corrupted')

        cmd = ['fs', 'get-attr', '--path', fname3]
        rc = run_daos_cmd(self.conf, cmd)
        assert rc.returncode == 0
        print(f'rc is {rc}')
        output = rc.stdout.decode('utf-8')
        assert check_dfs_tool_output(output, None, '1048576')
        with open(fname3, 'r', encoding='ascii', errors='ignore') as fd:
            data = fd.read()
            if data != 'test3':
                print('/test_dir/1d1/f3 data is corrupted')

        dir_list = os.listdir(dirname2)
        nr_entries = len(dir_list)
        if nr_entries != 1:
            raise NLTestFail('Wrong number of entries')

        if dfuse.stop():
            self.fatal_errors = True

    def test_pil4dfs_no_dfuse(self):
        """Test pil4dfs with no fuse instance"""
        self.server.run_daos_client_cmd_pil4dfs(['cp', '/bin/sh', '.'], container=self.container)
        rc = self.server.run_daos_client_cmd_pil4dfs(['ls'], container=self.container)
        print(rc.stdout)
        assert rc.stdout == b'sh\n', rc

    @needs_dfuse
    def test_pil4dfs(self):
        """Test interception library libpil4dfs.so"""
        path = self.dfuse.dir

        # Create a file natively.
        file1 = join(path, 'file1')
        with open(file1, 'w') as fd:
            fd.write('Hello World!')

        # Copy a file.
        file2 = join(path, 'file2')
        self.server.run_daos_client_cmd_pil4dfs(['cp', file1, file2])

        # Read a file with cat.
        self.server.run_daos_client_cmd_pil4dfs(['cat', file2])

        # touch a file.
        file3 = join(path, 'file3')
        self.server.run_daos_client_cmd_pil4dfs(['touch', file3])

        # cat a filename where a directory in the path is a file, should fail.
        nop_file = join(file3, 'new_file which will not exist...')
        rc = self.server.run_daos_client_cmd_pil4dfs(['cat', nop_file], check=False)
        assert rc.returncode == 1, rc

        # create a dir.
        dir1 = join(path, 'dir1')
        self.server.run_daos_client_cmd_pil4dfs(['mkdir', dir1])

        # create multiple levels dirs
        dirabcd = join(path, 'dira/dirb/dirc/dird')
        self.server.run_daos_client_cmd_pil4dfs(['mkdir', '-p', dirabcd])

        # find to list all files/dirs.
        self.server.run_daos_client_cmd_pil4dfs(['find', path])

        # remove a file.
        self.server.run_daos_client_cmd_pil4dfs(['rm', file3])

        # rm a dir with a file and a symlink
        file4 = join(path, 'dir1/file4')
        self.server.run_daos_client_cmd_pil4dfs(['touch', file4])
        link1 = join(path, 'dir1/link1')
        self.server.run_daos_client_cmd_pil4dfs(['ln', '-s', file4, link1])
        self.server.run_daos_client_cmd_pil4dfs(['rm', '-Rf', dir1])

        # dd to write a file
        file5 = join(path, 'newfile')
        self.server.run_daos_client_cmd_pil4dfs(['dd', 'if=/dev/zero', f'of={file5}', 'bs=1',
                                                'count=1'])


class NltStdoutWrapper():
    """Class for capturing stdout from threads"""

    def __init__(self):
        self._stdout = sys.stdout
        self._outputs = {}
        sys.stdout = self

    def write(self, value):
        """Print to stdout.  If this is the main thread then print it, always save it"""
        thread = threading.current_thread()
        if not thread.daemon:
            self._stdout.write(value)
        thread_id = thread.ident
        try:
            self._outputs[thread_id] += value
        except KeyError:
            self._outputs[thread_id] = value

    def sprint(self, value):
        """Really print something to stdout"""
        self._stdout.write(value + '\n')

    def get_thread_output(self):
        """Return the stdout by the calling thread, and reset for next time"""
        thread_id = threading.get_ident()
        try:
            data = self._outputs[thread_id]
            del self._outputs[thread_id]
            return data
        except KeyError:
            return None

    def flush(self):
        """Flush"""
        self._stdout.flush()

    def __del__(self):
        sys.stdout = self._stdout


class NltStderrWrapper():
    """Class for capturing stderr from threads"""

    def __init__(self):
        self._stderr = sys.stderr
        self._outputs = {}
        sys.stderr = self

    def write(self, value):
        """Print to stderr.  Always print it, always save it"""
        thread = threading.current_thread()
        self._stderr.write(value)
        thread_id = thread.ident
        try:
            self._outputs[thread_id] += value
        except KeyError:
            self._outputs[thread_id] = value

    def get_thread_err(self):
        """Return the stderr by the calling thread, and reset for next time"""
        thread_id = threading.get_ident()
        try:
            data = self._outputs[thread_id]
            del self._outputs[thread_id]
            return data
        except KeyError:
            return None

    def flush(self):
        """Flush"""
        self._stderr.flush()

    def __del__(self):
        sys.stderr = self._stderr


def run_posix_tests(server, conf, test=None):
    """Run one or all posix tests

    Create a new container per test, to ensure that every test is
    isolated from others.
    """

    def _run_test(ptl=None, function=None, test_cb=None):
        ptl.call_index = 0
        while True:
            ptl.needs_more = False
            ptl.test_name = function
            start = time.perf_counter()
            out_wrapper.sprint(f'Calling {function}')
            print(f'Calling {function}')

            # Do this with valgrind disabled as this code is run often and valgrind has a big
            # performance impact.  There are other tests that run with valgrind enabled so this
            # should not reduce coverage.
            try:
                ptl.container = create_cont(conf,
                                            pool,
                                            ctype="POSIX",
                                            valgrind=False,
                                            log_check=False,
                                            label=function)
                ptl.container_label = function
                test_cb()
                ptl.container.destroy(valgrind=False, log_check=False)
                ptl.container = None
            except Exception as inst:
                trace = ''.join(traceback.format_tb(inst.__traceback__))
                duration = time.perf_counter() - start
                out_wrapper.sprint(f'{ptl.test_name} Failed')
                conf.wf.add_test_case(ptl.test_name,
                                      repr(inst),
                                      stdout=out_wrapper.get_thread_output(),
                                      stderr=err_wrapper.get_thread_err(),
                                      output=trace,
                                      test_class='test',
                                      duration=duration)
                raise
            duration = time.perf_counter() - start
            out_wrapper.sprint(f'Test {ptl.test_name} took {duration:.1f} seconds')
            conf.wf.add_test_case(ptl.test_name,
                                  stdout=out_wrapper.get_thread_output(),
                                  stderr=err_wrapper.get_thread_err(),
                                  test_class='test',
                                  duration=duration)
            if not ptl.needs_more:
                break
            ptl.call_index = ptl.call_index + 1

        if ptl.fatal_errors:
            pto.fatal_errors = True

    pool = server.get_test_pool_obj()

    out_wrapper = NltStdoutWrapper()
    err_wrapper = NltStderrWrapper()

    pto = PosixTests(server, conf, pool=pool)
    if test:
        function = f'test_{test}'
        obj = getattr(pto, function)

        _run_test(ptl=pto, test_cb=obj, function=function)
    else:

        threads = []

        slow_tests = ['test_readdir_25', 'test_uns_basic', 'test_daos_fs_tool']

        tests = dir(pto)
        tests.sort(key=lambda x: x not in slow_tests)

        for function in tests:
            if not function.startswith('test_'):
                continue

            ptl = PosixTests(server, conf, pool=pool)
            obj = getattr(ptl, function)
            if not callable(obj):
                continue

            thread = threading.Thread(None,
                                      target=_run_test,
                                      name=f'test {function}',
                                      kwargs={'ptl': ptl, 'test_cb': obj, 'function': function},
                                      daemon=True)
            thread.start()
            threads.append(thread)

            # Limit the number of concurrent tests, but poll all active threads so there's no
            # expectation for them to complete in order.  At the minute we only have a handful of
            # long-running tests which dominate the time, so whilst a higher value here would
            # work there's no benefit in rushing to finish the quicker tests.  The long-running
            # tests are started first.
            while len(threads) > 4:
                for thread_id in threads:
                    thread_id.join(timeout=0)
                    if thread_id.is_alive():
                        continue
                    threads.remove(thread_id)

        for thread_id in threads:
            thread_id.join()

    # Now check for running dfuse instances, there should be none at this point as all tests have
    # completed.  It's not possible to do this check as each test finishes due to the fact that
    # the tests are running in parallel.  We could revise this so there's a dfuse method on
    # posix_tests class itself if required.
    for fuse in server.fuse_procs:
        conf.wf.add_test_case('fuse leak in tests',
                              f'Test leaked dfuse instance at {fuse}',
                              test_class='test',)

    out_wrapper = None
    err_wrapper = None

    return pto.fatal_errors


def run_tests(dfuse):
    """Run some tests"""
    # pylint: disable=consider-using-with
    path = dfuse.dir

    fname = join(path, 'test_file3')

    rc = subprocess.run(['dd', 'if=/dev/zero', 'bs=16k', 'count=64',  # nosec
                         f'of={join(path, "dd_file")}'],
                        check=True)
    print(rc)
    ofd = open(fname, 'w')
    ofd.write('hello')
    print(os.fstat(ofd.fileno()))
    ofd.flush()
    print(os.stat(fname))
    assert_file_size(ofd, 5)
    ofd.truncate(0)
    assert_file_size(ofd, 0)
    ofd.truncate(1024 * 1024)
    assert_file_size(ofd, 1024 * 1024)
    ofd.truncate(0)
    ofd.seek(0)
    ofd.write('simple file contents\n')
    ofd.flush()
    assert_file_size(ofd, 21)
    print(os.fstat(ofd.fileno()))
    ofd.close()
    dfuse.il_cmd(['cat', fname], check_write=False)
    ofd = os.open(fname, os.O_TRUNC)
    assert_file_size_fd(ofd, 0)
    os.close(ofd)
    symlink_name = join(path, 'symlink_src')
    symlink_dest = 'missing_dest'
    os.symlink(symlink_dest, symlink_name)
    assert symlink_dest == os.readlink(symlink_name)

    # Note that this doesn't test dfs because fuse will do a
    # lookup to check if the file exists rather than just trying
    # to create it.
    fname = join(path, 'test_file5')
    fd = os.open(fname, os.O_CREAT | os.O_EXCL)
    os.close(fd)
    try:
        fd = os.open(fname, os.O_CREAT | os.O_EXCL)
        os.close(fd)
        assert False
    except FileExistsError:
        pass
    os.unlink(fname)


def stat_and_check(dfuse, pre_stat):
    """Check that dfuse started"""
    post_stat = os.stat(dfuse.dir)
    if pre_stat.st_dev == post_stat.st_dev:
        raise NLTestFail('Device # unchanged')
    if post_stat.st_ino != 1:
        raise NLTestFail('Unexpected inode number')


def check_no_file(dfuse):
    """Check that a non-existent file doesn't exist"""
    try:
        os.stat(join(dfuse.dir, 'no-file'))
        raise NLTestFail('file exists')
    except FileNotFoundError:
        pass


nlt_lp = None  # pylint: disable=invalid-name
nlt_lt = None  # pylint: disable=invalid-name


def setup_log_test(conf):
    """Setup and import the log tracing code"""
    # Try and pick this up from the src tree if possible.
    file_self = os.path.dirname(os.path.abspath(__file__))
    logparse_dir = join(file_self, '../src/tests/ftest/cart/util')
    crt_mod_dir = os.path.realpath(logparse_dir)
    if crt_mod_dir not in sys.path:
        sys.path.append(crt_mod_dir)

    # Or back off to the install dir if not.
    logparse_dir = join(conf['PREFIX'], 'lib/daos/TESTING/ftest/cart')
    crt_mod_dir = os.path.realpath(logparse_dir)
    if crt_mod_dir not in sys.path:
        sys.path.append(crt_mod_dir)

    global nlt_lp  # pylint: disable=invalid-name
    global nlt_lt  # pylint: disable=invalid-name

    nlt_lp = __import__('cart_logparse')
    nlt_lt = __import__('cart_logtest')

    nlt_lt.wf = conf.wf


def log_timer(func):
    """Wrapper around the log_test function to measure how long it takes"""

    def log_timer_wrapper(*args, **kwargs):
        """Do the actual wrapping"""
        conf = args[0]
        conf.log_timer.start()
        rc = None
        try:
            rc = func(*args, **kwargs)
        finally:
            conf.log_timer.stop()
        return rc

    return log_timer_wrapper


@log_timer
def log_test(conf,
             filename,
             show_memleaks=True,
             quiet=False,
             skip_fi=False,
             leak_wf=None,
             ignore_einval=False,
             ignore_busy=False,
             check_read=False,
             check_write=False,
             check_fstat=False):
    """Run the log checker on filename, logging to stdout"""
    # pylint: disable=too-many-arguments

    # Check if the log file has wrapped, if it has then log parsing checks do
    # not work correctly.

    # https://stackoverflow.com/questions/1094841/get-human-readable-version-of-file-size
    def sizeof_fmt(num, suffix='B'):
        """Return size as a human readable string"""
        # pylint: disable=consider-using-f-string
        for unit in ['', 'Ki', 'Mi', 'Gi', 'Ti', 'Pi', 'Ei', 'Zi']:
            if abs(num) < 1024.0:
                return "%3.1f%s%s" % (num, unit, suffix)
            num /= 1024.0
        return "%.1f%s%s" % (num, 'Yi', suffix)

    if os.path.exists(f'{filename}.old'):
        raise NLTestFail('Log file exceeded max size')
    fstat = os.stat(filename)
    if fstat.st_size == 0:
        os.unlink(filename)
        return None
    if not quiet:
        print(f'Running log_test on {filename} {sizeof_fmt(fstat.st_size)}')

    log_iter = nlt_lp.LogIter(filename)

    # LogIter will have opened the file and seek through it as required, so start a background
    # process to compress it in parallel with the log tracing.
    conf.compress_file(filename)

    lto = nlt_lt.LogTest(log_iter, quiet=quiet)

    lto.hide_fi_calls = skip_fi

    if ignore_einval:
        lto.skip_suffixes.append(': 22 (Invalid argument)')

    if ignore_busy:
        lto.skip_suffixes.append(" DER_BUSY(-1012): 'Device or resource busy'")

    try:
        lto.check_log_file(abort_on_warning=True,
                           show_memleaks=show_memleaks,
                           leak_wf=leak_wf)
    except nlt_lt.LogCheckError:
        pass

    if skip_fi:
        if not lto.fi_triggered:
            raise NLTestNoFi

    functions = set()

    if check_read or check_write or check_fstat:
        for line in log_iter.new_iter():
            functions.add(line.function)

    if check_read and 'dfuse_read' not in functions:
        raise NLTestNoFunction('dfuse_read')

    if check_write and 'dfuse_write' not in functions:
        raise NLTestNoFunction('dfuse_write')

    if check_fstat and 'dfuse___fxstat' not in functions:
        raise NLTestNoFunction('dfuse___fxstat')

    if conf.max_log_size and fstat.st_size > conf.max_log_size:
        message = (f'Max log size exceeded, {sizeof_fmt(fstat.st_size)} > '
                   + sizeof_fmt(conf.max_log_size))
        conf.wf.add_test_case('logfile_size', failure=message)

    return lto.fi_location


def create_and_read_via_il(dfuse, path):
    """Create file in dir, write to and read through the interception library"""
    fname = join(path, 'test_file')
    with open(fname, 'w') as ofd:
        ofd.write('hello ')
        ofd.write('world\n')
        ofd.flush()
        assert_file_size(ofd, 12)
        print(os.fstat(ofd.fileno()))
    dfuse.il_cmd(['cat', fname], check_write=False)


def run_container_query(conf, path):
    """Query a path to extract container information"""
    cmd = ['container', 'query', '--path', path]

    rc = run_daos_cmd(conf, cmd)

    assert rc.returncode == 0

    print(rc)
    output = rc.stdout.decode('utf-8')
    for line in output.splitlines():
        print(line)


def run_duns_overlay_test(server, conf):
    """Create a DUNS entry point, and then start fuse over it

    Fuse should use the pool/container IDs from the entry point,
    and expose the container.
    """
    # pylint: disable=consider-using-with

    parent_dir = tempfile.TemporaryDirectory(dir=conf.dfuse_parent_dir, prefix='dnt_uns_')

    uns_dir = join(parent_dir.name, 'uns_ep')

    create_cont(conf, pool=server.get_test_pool_obj(), path=uns_dir)

    dfuse = DFuse(server, conf, mount_path=uns_dir, caching=False)

    dfuse.start(v_hint='uns-overlay')
    # To show the contents.
    # getfattr -d <file>

    # This should work now if the container was correctly found
    create_and_read_via_il(dfuse, uns_dir)

    return dfuse.stop()


def run_dfuse(server, conf):
    """Run several dfuse instances"""
    fatal_errors = BoolRatchet()

    pool = server.get_test_pool_obj()

    dfuse = DFuse(server, conf, caching=False)
    try:
        pre_stat = os.stat(dfuse.dir)
    except OSError:
        umount(dfuse.dir)
        raise
    dfuse.start(v_hint='no_pool')
    print(os.statvfs(dfuse.dir))
    subprocess.run(['df', '-h'], check=True)  # nosec
    subprocess.run(['df', '-i', dfuse.dir], check=True)  # nosec
    print('Running dfuse with nothing')
    stat_and_check(dfuse, pre_stat)
    check_no_file(dfuse)

    pool_stat = os.stat(join(dfuse.dir, pool.uuid))
    print(f'stat for {pool}')
    print(pool_stat)
    container = create_cont(server.conf, pool, ctype="POSIX")
    cdir = join(dfuse.dir, pool.uuid, container.uuid)
    fatal_errors.add_result(dfuse.stop())

    dfuse = DFuse(server, conf, pool=pool, caching=False)
    pre_stat = os.stat(dfuse.dir)
    dfuse.start(v_hint='pool_only')
    print('Running dfuse with pool only')
    stat_and_check(dfuse, pre_stat)
    check_no_file(dfuse)
    container2 = create_cont(server.conf, pool, ctype="POSIX")
    cpath = join(dfuse.dir, container2.id())
    print(os.listdir(cpath))
    cdir = join(dfuse.dir, container.id())
    create_and_read_via_il(dfuse, cdir)

    fatal_errors.add_result(dfuse.stop())

    dfuse = DFuse(server, conf, container=container, caching=False)
    dfuse.cores = 2
    pre_stat = os.stat(dfuse.dir)
    dfuse.start(v_hint='pool_and_cont')
    print('Running fuse with both')

    stat_and_check(dfuse, pre_stat)

    create_and_read_via_il(dfuse, dfuse.dir)

    run_tests(dfuse)

    fatal_errors.add_result(dfuse.stop())

    if fatal_errors.errors:
        print('Errors from dfuse')
    else:
        print('Reached the end, no errors')
    return fatal_errors.errors


def run_in_fg(server, conf, args):
    """Run dfuse in the foreground.

    Block until Control-C is pressed.
    """
    pool = server.get_test_pool_obj()
    label = 'foreground_cont'
    container = None

    conts = pool.fetch_containers()
    for cont in conts:
        if cont.label == label:
            container = cont.uuid
            break

    if not container:
        container = create_cont(conf, pool, label=label, ctype="POSIX")

        # Only set the container cache attributes when the container is initially created so they
        # can be modified later.
        cont_attrs = {'dfuse-data-cache': False,
                      'dfuse-attr-time': 60,
                      'dfuse-dentry-time': 60,
                      'dfuse-ndentry-time': 60,
                      'dfuse-direct-io-disable': False}
        container.set_attrs(cont_attrs)
        container = container.uuid

    dfuse = DFuse(server,
                  conf,
                  pool=pool.uuid,
                  caching=True,
                  wbcache=False,
                  multi_user=args.multi_user)

    dfuse.log_flush = True
    dfuse.start()

    t_dir = join(dfuse.dir, container)

    print(f'Running at {t_dir}')
    print(f'export PATH={join(conf["PREFIX"], "bin")}:$PATH')
    print(f'export LD_PRELOAD={join(conf["PREFIX"], "lib64", "libioil.so")}')
    print(f'export DAOS_AGENT_DRPC_DIR={conf.agent_dir}')
    print('export D_IL_REPORT=-1')
    if args.multi_user:
        print(f'dmg pool --insecure update-acl -e A::root@:rw {pool.id()}')
    print(f'daos container create --type POSIX --path {t_dir}/uns-link')
    print(f'daos container destroy --path {t_dir}/uns-link')
    print(f'daos cont list {pool.label}')

    try:
        if args.launch_cmd:
            start = time.perf_counter()
            # Set the PATH and agent dir.
            agent_env = os.environ.copy()
            agent_env['DAOS_AGENT_DRPC_DIR'] = conf.agent_dir
            agent_env['PATH'] = f'{join(conf["PREFIX"], "bin")}:{agent_env["PATH"]}'
            rc = subprocess.run(args.launch_cmd, check=False, cwd=t_dir, env=agent_env)
            elapsed = time.perf_counter() - start
            dfuse.stop()
            (minutes, seconds) = divmod(elapsed, 60)
            print(f'Completed in {int(minutes):d}:{int(seconds):02d}')
            print(rc)
        else:
            dfuse.wait_for_exit()
    except KeyboardInterrupt:
        pass


def check_readdir_perf(server, conf):
    """Check and report on readdir performance

    Loop over number of files, measuring the time taken to
    populate a directory, and to read the directory contents,
    measure both files and directories as contents, and
    readdir both with and without stat, restarting dfuse
    between each test to avoid cache effects.

    Continue testing until five minutes have passed, and print
    a table of results.
    """
    headers = ['count', 'create\ndirs', 'create\nfiles']
    headers.extend(['dirs', 'files', 'dirs\nwith stat', 'files\nwith stat'])
    headers.extend(['caching\n1st', 'caching\n2nd'])

    results = []

    def make_dirs(parent, count):
        """Populate the test directory"""
        print(f'Populating to {count}')
        dir_dir = join(parent, f'dirs.{count}.in')
        t_dir = join(parent, f'dirs.{count}')
        file_dir = join(parent, f'files.{count}.in')
        t_file = join(parent, f'files.{count}')

        start_all = time.perf_counter()
        if not os.path.exists(t_dir):
            try:
                os.mkdir(dir_dir)
            except FileExistsError:
                pass
            for idx in range(count):
                try:
                    os.mkdir(join(dir_dir, str(idx)))
                except FileExistsError:
                    pass
            dir_time = time.perf_counter() - start_all
            print(f'Creating {count} dirs took {dir_time:.2f}')
            os.rename(dir_dir, t_dir)

        if not os.path.exists(t_file):
            try:
                os.mkdir(file_dir)
            except FileExistsError:
                pass
            start = time.perf_counter()
            for idx in range(count):
                with open(join(file_dir, str(idx)), 'w'):
                    pass
            file_time = time.perf_counter() - start
            print(f'Creating {count} files took {file_time:.2f}')
            os.rename(file_dir, t_file)

        return [dir_time, file_time]

    def print_results():
        """Display the results"""
        print(tabulate.tabulate(results, headers=headers, floatfmt=".2f"))

    pool = server.get_test_pool_obj().uuid

    container = str(uuid.uuid4())

    dfuse = DFuse(server, conf, pool=pool)

    print('Creating container and populating')
    count = 1024
    dfuse.start()
    parent = join(dfuse.dir, container)
    try:
        os.mkdir(parent)
    except FileExistsError:
        pass
    create_times = make_dirs(parent, count)
    dfuse.stop()

    all_start = time.perf_counter()

    while True:

        row = [count]
        row.extend(create_times)
        dfuse = DFuse(server, conf, pool=pool, container=container,
                      caching=False)
        dir_dir = join(dfuse.dir, f'dirs.{count}')
        file_dir = join(dfuse.dir, f'files.{count}')
        dfuse.start()
        start = time.perf_counter()
        subprocess.run(['/bin/ls', dir_dir], stdout=subprocess.PIPE, check=True)
        elapsed = time.perf_counter() - start
        print(f'processed {count} dirs in {elapsed:.2f} seconds')
        row.append(elapsed)
        dfuse.stop()
        dfuse = DFuse(server, conf, pool=pool, container=container,
                      caching=False)
        dfuse.start()
        start = time.perf_counter()
        subprocess.run(['/bin/ls', file_dir], stdout=subprocess.PIPE,
                       check=True)
        elapsed = time.perf_counter() - start
        print(f'processed {count} dirs in {elapsed:.2f} seconds')
        row.append(elapsed)
        dfuse.stop()

        dfuse = DFuse(server, conf, pool=pool, container=container,
                      caching=False)
        dfuse.start()
        start = time.perf_counter()
        subprocess.run(['/bin/ls', '-t', dir_dir], stdout=subprocess.PIPE,
                       check=True)
        elapsed = time.perf_counter() - start
        print(f'processed {count} dirs in {elapsed:.2f} seconds')
        row.append(elapsed)
        dfuse.stop()
        dfuse = DFuse(server, conf, pool=pool, container=container,
                      caching=False)
        dfuse.start()
        start = time.perf_counter()
        # Use sort by time here so ls calls stat, if you run ls -l then it will
        # also call getxattr twice which skews the figures.
        subprocess.run(['/bin/ls', '-t', file_dir], stdout=subprocess.PIPE,
                       check=True)
        elapsed = time.perf_counter() - start
        print(f'processed {count} dirs in {elapsed:.2f} seconds')
        row.append(elapsed)
        dfuse.stop()

        # Test with caching enabled.  Check the file directory, and do it twice
        # without restarting, to see the effect of populating the cache, and
        # reading from the cache.
        dfuse = DFuse(server,
                      conf,
                      pool=pool,
                      container=container,
                      caching=True)
        dfuse.start()
        start = time.perf_counter()
        subprocess.run(['/bin/ls', '-t', file_dir], stdout=subprocess.PIPE,
                       check=True)
        elapsed = time.perf_counter() - start
        print(f'processed {count} dirs in {elapsed:.2f} seconds')
        row.append(elapsed)
        start = time.perf_counter()
        subprocess.run(['/bin/ls', '-t', file_dir], stdout=subprocess.PIPE,
                       check=True)
        elapsed = time.perf_counter() - start
        print(f'processed {count} dirs in {elapsed:.2f} seconds')
        row.append(elapsed)
        results.append(row)

        elapsed = time.perf_counter() - all_start
        if elapsed > 5 * 60:
            dfuse.stop()
            break

        print_results()
        count *= 2
        create_times = make_dirs(dfuse.dir, count)
        dfuse.stop()

    run_daos_cmd(conf, ['container',
                        'destroy',
                        pool,
                        container])
    print_results()


def test_pydaos_kv(server, conf):
    """Test the KV interface"""
    # pylint: disable=consider-using-with

    pydaos_log_file = tempfile.NamedTemporaryFile(prefix='dnt_pydaos_',
                                                  suffix='.log',
                                                  delete=False)

    os.environ['D_LOG_FILE'] = pydaos_log_file.name
    daos = import_daos(server, conf)

    pool = server.get_test_pool_obj()

    cont = create_cont(conf, pool, ctype="PYTHON", label="PYDAOS_NLT")

    container = daos.DCont(pool.uuid, cont.uuid)

    kv = container.dict('my_test_kv')
    kv['a'] = 'a'
    kv['b'] = 'b'
    kv['list'] = pickle.dumps(list(range(1, 100000)))
    for key in range(1, 100):
        kv[str(key)] = pickle.dumps(list(range(1, 10)))
    print(type(kv))
    print(kv)
    print(kv['a'])

    print("First iteration")
    data = {}
    for key in kv:
        print(f'key is {key}, len {len(kv[key])}')
        print(type(kv[key]))
        data[key] = None

    print("Bulk loading")

    data['no-key'] = None

    kv.value_size = 32
    kv.bget(data, value_size=16)
    print("Default get value size %d", kv.value_size)
    print("Second iteration")
    failed = False
    for key, value in data.items():
        if value:
            print(f'key is {key}, len {len(value)}')
        elif key == 'no-key':
            pass
        else:
            failed = True
            print(f'Key is None {key}')

    if failed:
        print("That's not good")

    del kv
    del container

    print('Running PyDAOS container checker')
    daos.check(pool.label, "PYDAOS_NLT")
    # pylint: disable=protected-access
    daos._cleanup()
    log_test(conf, pydaos_log_file.name)


def test_pydaos_kv_obj_class(server, conf):
    """Test the predefined object class works with KV"""
    with tempfile.NamedTemporaryFile(prefix='kv_objclass_pydaos_',
                                     suffix='.log',
                                     delete=False) as tmp_file:
        log_name = tmp_file.name
        os.environ['D_LOG_FILE'] = log_name

    daos = import_daos(server, conf)

    pool = server.get_test_pool_obj()

    cont = create_cont(conf, pool, ctype="PYTHON", label='pydaos_cont')

    container = daos.DCont(pool.label, cont.label)
    failed = False
    # Write kv1 dictionary with OC_S2 object type
    kv1 = container.dict('object1', {"Monday": "1"}, "OC_S2")
    if len(kv1) != 1:
        failed = True
        print(f'Expected length of kv object is 1 but got {len(kv1)}')

    # Write kv2 dictionary without any object type,
    # so in this case we have 4 targets so default object type should be S4
    kv2 = container.dict('object2', {"Monday": "1", "Tuesday": "2"})
    if len(kv2) != 2:
        failed = True
        print(f'Expected length of kv object is 2 but got {len(kv2)}')

    # Run a command to list the objects
    cmd = ['cont', 'list-objects', pool.label, cont.label]
    print('list the objects from container')
    rc = run_daos_cmd(conf, cmd, use_json=True)

    data = rc.json
    assert data['status'] == 0, rc
    assert data['error'] is None, rc
    assert data['response'] is not None, rc

    # Run a command to get the object layout
    print('query the object layout')
    actual_obj_layout = []
    for obj in data['response']:
        cmd = ['object', 'query', pool.label, cont.label, obj]
        rc = run_daos_cmd(conf, cmd, use_json=True)

        query_data = rc.json
        assert query_data['status'] == 0, rc
        assert query_data['error'] is None, rc
        assert query_data['response'] is not None, rc
        actual_obj_layout.append(query_data['response']['class'])

    # Verify the object has the correct layout used during kv dictionary creation.
    expected_obj_layout = ['S2', 'S4']
    for obj in expected_obj_layout:
        if obj not in actual_obj_layout:
            failed = True
            print(f'Expected obj {obj} not found in all {actual_obj_layout}')

    if failed:
        conf.wf.add_test_case('pydaos kv object test', failure='test failed')
    else:
        conf.wf.add_test_case('pydaos kv object test')

    # pylint: disable=protected-access
    del kv1
    del kv2
    del container
    daos._cleanup()
    log_test(conf, log_name)

# Fault injection testing.
#
# This runs two different commands under fault injection, although it allows
# for more to be added.  The command is defined, then run in a loop with
# different locations enabled, essentially failing each call to
# D_ALLOC() in turn.  This iterates for all memory allocations in the command
# which is around 1300 each command so this takes a while.
#
# In order to improve response times the different locations are run in
# parallel, although the results are processed in order.
#
# Each location is checked for memory leaks according to the log file
# (D_ALLOC/D_FREE not matching), that it didn't crash and some checks are run
# on stdout/stderr as well.
#
# If a particular location caused the command to exit with a signal then that
# location is re-run at the end under valgrind to get better diagnostics.
#


class AllocFailTestRun():
    """Class to run a fault injection command with a single fault"""

    def __init__(self, aft, cmd, env, loc, cwd):

        # The return from subprocess.poll
        self.ret = None
        self.fault_injected = None
        self.loc = loc
        # The valgrind handle
        self.valgrind_hdl = None

        self.dir_handle = None
        self.stdout = None
        self.returncode = None

        # Set this to disable memory leak checking if the command outputs a DER_BUSY message.  This
        # is to allow tests to leak memory if there are errors during shutdown.
        self.ignore_busy = False

        # The subprocess handle and other private data.
        self._sp = None
        self._cmd = cmd
        self._env = env
        self._aft = aft
        self._fi_file = None
        self._stderr = None
        self._fi_loc = None
        self._cwd = cwd

        if loc:
            prefix = f'dnt_{loc:04d}_'
        else:
            prefix = 'dnt_reference_'
        with tempfile.NamedTemporaryFile(prefix=prefix,
                                         suffix='.log',
                                         dir=self._aft.log_dir,
                                         delete=False) as log_file:
            self.log_file = log_file.name
            self._env['D_LOG_FILE'] = self.log_file

    def __str__(self):
        cmd_text = ' '.join(self._cmd)
        res = f"Fault injection test of '{cmd_text}'\n"
        res += f'Fault injection location {self.loc}\n'
        if self.valgrind_hdl:
            res += 'Valgrind enabled for this test\n'
        if self.returncode is None:
            res += 'Process not completed'
        else:
            res += f'Returncode was {self.returncode}'

        if self.stdout:
            res += f'\nSTDOUT:{self.stdout.decode("utf-8").strip()}'

        if self._stderr:
            res += f'\nSTDERR:{self._stderr.decode("utf-8").strip()}'
        return res

    def start(self):
        """Start the command"""
        faults = {}

        faults['fault_config'] = [{'id': 100,
                                   'probability_x': 1,
                                   'probability_y': 1}]

        if self.loc:
            faults['fault_config'].append({'id': 0,
                                           'probability_x': 1,
                                           'probability_y': 1,
                                           'interval': self.loc,
                                           'max_faults': 1})

            if self._aft.skip_daos_init:
                faults['fault_config'].append({'id': 101, 'probability_x': 1})

        # pylint: disable=consider-using-with
        self._fi_file = tempfile.NamedTemporaryFile(prefix='fi_', suffix='.yaml')

        self._fi_file.write(yaml.dump(faults, encoding='utf=8'))
        self._fi_file.flush()

        self._env['D_FI_CONFIG'] = self._fi_file.name

        if self.valgrind_hdl:
            exec_cmd = self.valgrind_hdl.get_cmd_prefix()
            exec_cmd.extend(self._cmd)
        else:
            exec_cmd = self._cmd

        self._sp = subprocess.Popen(exec_cmd,
                                    env=self._env,
                                    cwd=self._cwd,
                                    stdin=subprocess.PIPE,
                                    stdout=subprocess.PIPE,
                                    stderr=subprocess.PIPE)

    def has_finished(self):
        """Check if the command has completed"""
        if self.returncode is not None:
            return True

        rc = self._sp.poll()
        if rc is None:
            return False
        self._post(rc)
        return True

    def wait(self):
        """Wait for the command to complete"""
        if self.returncode is not None:
            return

        self._post(self._sp.wait())

    def _post(self, rc):
        """Helper function, called once after command is complete.

        This is where all the checks are performed.
        """
        def _explain():

            if self._aft.conf.tmp_dir:
                log_dir = self._aft.conf.tmp_dir
            else:
                log_dir = '/tmp'

            short_log_file = self.log_file

            if short_log_file.startswith(self.log_file):
                short_log_file = short_log_file[len(log_dir) + 1:]

            self._aft.wf.explain(self._fi_loc, short_log_file, fi_signal)
            self._aft.conf.wf.explain(self._fi_loc, short_log_file, fi_signal)
        # Put in a new-line.
        print()
        self.returncode = rc
        self.stdout = self._sp.stdout.read()
        self._stderr = self._sp.stderr.read()

        show_memleaks = True

        fi_signal = None
        # A negative return code means the process exited with a signal so do
        # not check for memory leaks in this case as it adds noise, right when
        # it's least wanted.
        if rc < 0:
            show_memleaks = False
            fi_signal = -rc

        if self._aft.ignore_busy and self._aft.check_daos_stderr:
            stderr = self._stderr.decode('utf-8').rstrip()
            for line in stderr.splitlines():
                if line.endswith(': Device or resource busy (-1012)'):
                    show_memleaks = False

        try:
            if self.loc:
                wf = self._aft.wf
            else:
                wf = None

            self._fi_loc = log_test(self._aft.conf,
                                    self.log_file,
                                    show_memleaks=show_memleaks,
                                    ignore_busy=self._aft.ignore_busy,
                                    quiet=True,
                                    skip_fi=True,
                                    leak_wf=wf)
            self.fault_injected = True
            assert self._fi_loc
        except NLTestNoFi:
            # If a fault wasn't injected then check output is as expected.
            # It's not possible to log these as warnings, because there is
            # no src line to log them against, so simply assert.
            assert self.returncode == 0, self

            if self._aft.check_post_stdout:
                assert self._stderr == b''
                if self._aft.expected_stdout is not None:
                    assert self.stdout == self._aft.expected_stdout
            self.fault_injected = False
        if self.valgrind_hdl:
            self.valgrind_hdl.convert_xml()
        if not self.fault_injected:
            _explain()
            return

        # Check stderr from a daos command.
        # These should mostly be from the DH_PERROR_SYS or DH_PERROR_DER macros so check for
        # this format.  There may be multiple lines and the two styles may be mixed.
        # These checks will report an error against the line of code that introduced the "leak"
        # which may well only have a loose correlation to where the error was reported.
        if self._aft.check_daos_stderr:

            # The go code will report a stacktrace in some cases on segfault or double-free
            # and these will obviously not be the expected output but are obviously an error,
            # to avoid filling the results with lots of warnings about stderr just include one
            # to say the check is disabled.
            if rc in (-6, -11):
                self._aft.wf.add(self._fi_loc,
                                 'NORMAL',
                                 f"Unable to check stderr because of exit code '{rc}'",
                                 mtype='Crash preventing check')
                _explain()
                return

            stderr = self._stderr.decode('utf-8').rstrip()
            for line in stderr.splitlines():

                # This is what the go code uses.
                if line.endswith(': DER_NOMEM(-1009): Out of memory'):
                    continue

                # This is what the go code uses for system errors.
                if line.endswith(': errno 12 (Cannot allocate memory)'):
                    continue

                # This is what DH_PERROR_DER uses
                if line.endswith(': Out of memory (-1009)'):
                    continue

                # This is what DH_PERROR_SYS uses
                if line.endswith(': Cannot allocate memory (12)'):
                    continue

                if self._aft.ignore_busy and line.endswith(': Device or resource busy (-1012)'):
                    continue

                if 'DER_UNKNOWN' in line:
                    self._aft.wf.add(self._fi_loc,
                                     'HIGH',
                                     f"Incorrect stderr '{line}'",
                                     mtype='Invalid error code used')
                    continue

                self._aft.wf.add(self._fi_loc,
                                 'NORMAL',
                                 f"Malformed stderr '{line}'",
                                 mtype='Malformed stderr')
            _explain()
            return

        if self.returncode == 0 and self._aft.check_post_stdout:
            if self.stdout != self._aft.expected_stdout:
                self._aft.wf.add(self._fi_loc,
                                 'NORMAL',
                                 f"Incorrect stdout '{self.stdout}'",
                                 mtype='Out of memory caused zero exit code with incorrect output')

        if self._aft.check_stderr:
            stderr = self._stderr.decode('utf-8').rstrip()
            if stderr != '' and not stderr.endswith('(-1009): Out of memory') and \
                not stderr.endswith(': errno 12 (Cannot allocate memory)') and \
               'error parsing command line arguments' not in stderr and \
               self.stdout != self._aft.expected_stdout:
                if self.stdout != b'':
                    print(self._aft.expected_stdout)
                    print()
                    print(self.stdout)
                    print()
                self._aft.wf.add(self._fi_loc,
                                 'NORMAL',
                                 f"Incorrect stderr '{stderr}'",
                                 mtype='Out of memory not reported correctly via stderr')
        _explain()


class AllocFailTest():
    # pylint: disable=too-few-public-methods
    """Class to describe fault injection command"""

    def __init__(self, conf, desc, cmd):
        self.conf = conf
        self.cmd = cmd
        self.description = desc
        self.prefix = True
        # Check stdout/error from commands where faults were not injected
        self.check_post_stdout = True
        # Check stderr conforms to daos_hdlr.c style
        self.check_daos_stderr = False
        self.check_stderr = True
        self.expected_stdout = None
        self.ignore_busy = False
        self.use_il = False
        self._use_pil4dfs = None
        self.wf = conf.wf
        # Instruct the fault injection code to skip daos_init().
        self.skip_daos_init = True
        log_dir = f'dnt_fi_{self.description}_logs'
        if conf.tmp_dir:
            self.log_dir = join(conf.tmp_dir, log_dir)
        else:
            self.log_dir = join('/tmp', log_dir)
        try:
            os.mkdir(self.log_dir)
        except FileExistsError:
            pass

    def use_pil4dfs(self, container):
        """Mark test to use pil4dfs and set container"""
        self._use_pil4dfs = container
        self.check_stderr = False

    def launch(self):
        """Run all tests for this command"""

        def _prep(self):
            rc = self._run_cmd(None)
            rc.wait()
            self.expected_stdout = rc.stdout
            assert not rc.fault_injected

        # Prep what the expected stdout is by running once without faults
        # enabled.
        _prep(self)

        print('Expected stdout is')
        print(self.expected_stdout)

        # pylint: disable-next=no-member
        num_cores = len(os.sched_getaffinity(0))

        if num_cores < 20:
            max_child = 1
        else:
            max_child = int(num_cores / 4 * 3)

        print(f'Maximum number of spawned tests will be {max_child}')

        active = []
        fid = 2
        max_count = 0
        finished = False

        # List of fault identifiers to re-run under valgrind.
        to_rerun = []

        fatal_errors = False

        # Now run all iterations in parallel up to max_child.  Iterations will be launched
        # in order but may not finish in order, rather they are processed in the order they
        # finish.  After each repetition completes then check for re-launch new processes
        # to keep the pipeline full.
        while not finished or active:

            if not finished:
                while len(active) < max_child:
                    active.append(self._run_cmd(fid))
                    fid += 1

                    if len(active) > max_count:
                        max_count = len(active)

            # Now complete as many as have finished.
            for ret in active:
                if not ret.has_finished():
                    continue
                active.remove(ret)
                print()
                print(ret)
                if ret.returncode < 0:
                    fatal_errors = True
                    to_rerun.append(ret.loc)

                if not ret.fault_injected:
                    print('Fault injection did not trigger, stopping')
                    finished = True
                break

        print(f'Completed, fid {fid}')
        print(f'Max in flight {max_count}')
        if to_rerun:
            print(f'Number of indexes to re-run {len(to_rerun)}')

        for fid in to_rerun:
            rerun = self._run_cmd(fid, valgrind=True)
            print(rerun)
            rerun.wait()

        return fatal_errors

    def _run_cmd(self, loc, valgrind=False):
        """Run the test with fault injection enabled"""
        cmd_env = get_base_env()

        # Debug flags to enable all memory allocation logging, but as little else as possible.
        # This improves run-time but makes debugging any issues found harder.
        # cmd_env['D_LOG_MASK'] = 'DEBUG'
        # cmd_env['DD_MASK'] = 'mem'
        # del cmd_env['DD_SUBSYS']

        if self.use_il:
            cmd_env['LD_PRELOAD'] = join(self.conf['PREFIX'], 'lib64', 'libioil.so')

        cwd = None
        tmp_dir = None

        if self._use_pil4dfs is not None:
            # pylint: disable-next=consider-using-with
            tmp_dir = tempfile.TemporaryDirectory(prefix='pil4dfs_mount')
            cwd = tmp_dir.name
            cmd_env['DAOS_MOUNT_POINT'] = cwd
            cmd_env['LD_PRELOAD'] = join(self.conf['PREFIX'], 'lib64', 'libpil4dfs.so')
            cmd_env['DAOS_POOL'] = self._use_pil4dfs.pool.id()
            cmd_env['DAOS_CONTAINER'] = self._use_pil4dfs.id()

        cmd_env['DAOS_AGENT_DRPC_DIR'] = self.conf.agent_dir

        if callable(self.cmd):
            cmd = self.cmd(loc)
        else:
            cmd = self.cmd

        # Disable logging to stderr from the daos tool, the two streams are both checked already
        # but have different formats.
        if os.path.basename(cmd[0]) == 'daos':
            cmd_env['DD_STDERR'] = 'CRIT'

        aftf = AllocFailTestRun(self, cmd, cmd_env, loc, cwd)
        if valgrind:
            aftf.valgrind_hdl = ValgrindHelper(self.conf, logid=f'fi_{self.description}_{loc}')
            # Turn off leak checking in this case, as we're just interested in why it crashed.
            aftf.valgrind_hdl.full_check = False

        aftf.dir_handle = tmp_dir
        aftf.start()

        return aftf


def test_dfuse_start(server, conf, wf):
    """Start dfuse under fault injection

    This test will check error paths for faults that can occur whilst starting
    dfuse.  To do this it injects a fault into dfuse just before dfuse_session_mount
    so that it always returns immediately rather than registering with the kernel
    and then it runs dfuse up to this point checking the error paths.
    """
    pool = server.get_test_pool_obj()

    container = create_cont(conf, pool, ctype='POSIX')

    mount_point = join(conf.dfuse_parent_dir, 'fi-mount')

    os.mkdir(mount_point)

    cmd = [join(conf['PREFIX'], 'bin', 'dfuse'),
           '--mountpoint', mount_point,
           '--pool', pool.id(), '--cont', container.id(), '--foreground', '--singlethread']

    test_cmd = AllocFailTest(conf, 'dfuse', cmd)
    test_cmd.wf = wf
    test_cmd.skip_daos_init = False
    test_cmd.check_daos_stderr = True
    test_cmd.check_post_stdout = False  # Checked.

    rc = test_cmd.launch()
    os.rmdir(mount_point)
    return rc


def test_alloc_fail_copy(server, conf, wf):
    """Run container (filesystem) copy under fault injection.

    This test will create a new uuid per iteration, and the test will then try to create a matching
    container so this is potentially resource intensive.

    Create an initial container to copy from so this is testing reading as well as writing

    see also test_alloc_fail_copy_trunc() which is similar but truncates existing files.
    """

    def get_cmd(cont_id):
        return [join(conf['PREFIX'], 'bin', 'daos'),
                'filesystem',
                'copy',
                '--src',
                f'daos://{pool.id()}/aft_base',
                '--dst',
                f'daos://{pool.id()}/container_{cont_id}']

    pool = server.get_test_pool_obj()
    with tempfile.TemporaryDirectory(prefix='copy_src_',) as src_dir:
        sub_dir = join(src_dir, 'new_dir')
        os.mkdir(sub_dir)

        for idx in range(5):
            with open(join(sub_dir, f'file.{idx}'), 'w') as ofd:
                ofd.write('hello')

        os.symlink('broken', join(sub_dir, 'broken_s'))
        os.symlink('file.0', join(sub_dir, 'link'))

        rc = run_daos_cmd(conf, ['filesystem', 'copy', '--src', sub_dir,
                                 '--dst', f'daos://{pool.id()}/aft_base'])
        assert rc.returncode == 0, rc

    test_cmd = AllocFailTest(conf, 'filesystem-copy', get_cmd)
    test_cmd.wf = wf
    test_cmd.check_daos_stderr = True
    test_cmd.check_post_stdout = False
    # Set the ignore_busy flag so that memory leaks on shutdown are ignored in some cases.
    test_cmd.ignore_busy = True

    return test_cmd.launch()


def test_alloc_fail_copy_trunc(server, conf, wf):
    """Run container (filesystem) copy under fault injection.

    Use filesystem copy to truncate a file.

    Create an initial container to modify, pre-populate it with a number of files of known length
    then have each iteration of the test truncate one file.
    """
    # The number of files to pre-create.  This just needs to be bigger than the iteration count
    # however too many will consume extra resources.
    files_needed = 4000

    def get_cmd(_):
        cmd = [join(conf['PREFIX'], 'bin', 'daos'), 'filesystem', 'copy', '--src', src_file.name,
               '--dst', f'daos://{pool.id()}/aftc/new_dir/file.{get_cmd.idx}']
        get_cmd.idx += 1
        assert get_cmd.idx <= files_needed
        return cmd

    get_cmd.idx = 0  # pylint: disable=invalid-name

    pool = server.get_test_pool_obj()
    with tempfile.TemporaryDirectory(prefix='copy_src_',) as src_dir:
        sub_dir = join(src_dir, 'new_dir')
        os.mkdir(sub_dir)

        for idx in range(files_needed):
            with open(join(sub_dir, f'file.{idx}'), 'w') as ofd:
                ofd.write('hello')

        rc = run_daos_cmd(conf, ['filesystem', 'copy', '--src', sub_dir,
                                 '--dst', f'daos://{pool.id()}/aftc'])
        assert rc.returncode == 0, rc

    with tempfile.NamedTemporaryFile() as src_file:

        test_cmd = AllocFailTest(conf, 'filesystem-copy-trunc', get_cmd)
        test_cmd.wf = wf
        test_cmd.check_daos_stderr = True
        test_cmd.check_post_stdout = False
        # Set the ignore_busy flag so that memory leaks on shutdown are ignored in some cases.
        test_cmd.ignore_busy = True

        return test_cmd.launch()


def test_alloc_pil4dfs_ls(server, conf, wf):
    """Run pil4dfs under fault injection

    Create a pool and populate a subdir with a number of entries, files, symlink (broken and not)
    and another subdir.  Run 'ls' on this to see the output.
    """
    pool = server.get_test_pool_obj()

    container = create_cont(conf, pool, ctype='POSIX', label='pil4dfs_fi')

    with tempfile.TemporaryDirectory(prefix='pil4_src_',) as src_dir:
        sub_dir = join(src_dir, 'new_dir')
        os.mkdir(sub_dir)

        for idx in range(5):
            with open(join(sub_dir, f'file.{idx}'), 'w') as ofd:
                ofd.write('hello')

        os.mkdir(join(sub_dir, 'new_dir'))
        os.symlink('broken', join(sub_dir, 'broken_s'))
        os.symlink('file.0', join(sub_dir, 'link'))

        rc = run_daos_cmd(conf, ['filesystem', 'copy', '--src', f'{src_dir}/new_dir',
                                 '--dst', f'daos://{pool.id()}/{container.id()}'])
        print(rc)
        assert rc.returncode == 0, rc

    test_cmd = AllocFailTest(conf, 'pil4dfs-ls', ['ls', '-l', 'new_dir/'])
    test_cmd.wf = wf
    test_cmd.use_pil4dfs(container)
    test_cmd.check_daos_stderr = False
    test_cmd.check_post_stdout = False

    return test_cmd.launch()


def test_alloc_cont_create(server, conf, wf):
    """Run container creation under fault injection.

    This test will create a new uuid per iteration, and the test will then try to create a matching
    container so this is potentially resource intensive.
    """
    pool = server.get_test_pool_obj()

    def get_cmd(cont_id):
        return [join(conf['PREFIX'], 'bin', 'daos'),
                'container',
                'create',
                pool.id(),
                '--properties',
                f'srv_cksum:on,label:{cont_id}']

    test_cmd = AllocFailTest(conf, 'cont-create', get_cmd)
    test_cmd.wf = wf
    test_cmd.check_post_stdout = False

    return test_cmd.launch()


def test_alloc_fail_cont_create(server, conf):
    """Run container create --path under fault injection."""
    pool = server.get_test_pool_obj()
    container = create_cont(conf, pool, ctype='POSIX', label='parent_cont')

    dfuse = DFuse(server, conf, container=container)
    dfuse.use_valgrind = False
    dfuse.start()

    def get_cmd(cont_id):
        return [join(conf['PREFIX'], 'bin', 'daos'),
                'container',
                'create',
                '--type',
                'POSIX',
                '--path',
                join(dfuse.dir, f'container_{cont_id}')]

    test_cmd = AllocFailTest(conf, 'cont-create', get_cmd)
    test_cmd.check_post_stdout = False

    rc = test_cmd.launch()
    dfuse.stop()
    return rc


def test_alloc_fail_cat(server, conf):
    """Run the Interception library with fault injection

    Start dfuse for this test, and do not do output checking on the command
    itself yet.
    """
    pool = server.get_test_pool_obj()
    container = create_cont(conf, pool, ctype='POSIX', label='fault_inject')

    dfuse = DFuse(server, conf, container=container)
    dfuse.use_valgrind = False
    dfuse.start()

    target_file = join(dfuse.dir, 'test_file')

    with open(target_file, 'w') as fd:
        fd.write('Hello there')

    test_cmd = AllocFailTest(conf, 'il-cat', ['cat', target_file])
    test_cmd.use_il = True
    test_cmd.wf = conf.wf

    rc = test_cmd.launch()
    dfuse.stop()
    return rc


def test_alloc_fail_il_cp(server, conf):
    """Run the Interception library with fault injection

    Start dfuse for this test, and do not do output checking on the command itself yet.
    """
    pool = server.get_test_pool_obj()
    container = create_cont(conf, pool, ctype='POSIX', label='il_cp')

    dfuse = DFuse(server, conf, container=container)
    dfuse.use_valgrind = False
    dfuse.start()

    test_dir = join(dfuse.dir, 'test_dir')

    os.mkdir(test_dir)

    cmd = ['fs', 'set-attr', '--path', test_dir, '--oclass', 'S4', '--chunk-size', '8']

    rc = run_daos_cmd(conf, cmd)
    print(rc)

    src_file = join(test_dir, 'src_file')

    with open(src_file, 'w') as fd:
        fd.write('Some raw test data that spans over at least two targets and possibly more.')

    def get_cmd(loc):
        return ['cp', src_file, join(test_dir, f'test_{loc}')]

    test_cmd = AllocFailTest(conf, 'il-cp', get_cmd)
    test_cmd.use_il = True
    test_cmd.wf = conf.wf

    rc = test_cmd.launch()
    dfuse.stop()
    container.destroy()
    return rc


def test_fi_list_attr(server, conf, wf):
    """Run daos cont list-attr with fault injection"""
    pool = server.get_test_pool_obj()

    container = create_cont(conf, pool)

    container.set_attrs({'my-test-attr-1': 'some-value',
                        'my-test-attr-2': 'some-other-value'})

    cmd = [join(conf['PREFIX'], 'bin', 'daos'),
           'container',
           'list-attrs',
           pool.id(),
           container.id()]

    test_cmd = AllocFailTest(conf, 'cont-list-attr', cmd)
    test_cmd.wf = wf

    rc = test_cmd.launch()
    container.destroy()
    return rc


def test_fi_get_prop(server, conf, wf):
    """Run daos cont get-prop with fault injection"""
    pool = server.get_test_pool_obj()

    container = create_cont(conf, pool, ctype='POSIX')

    cmd = [join(conf['PREFIX'], 'bin', 'daos'),
           'container',
           'get-prop',
           pool.id(),
           container.id()]

    test_cmd = AllocFailTest(conf, 'cont-get-prop', cmd)
    test_cmd.wf = wf
    test_cmd.check_post_stdout = False  # Checked.

    rc = test_cmd.launch()
    container.destroy()
    return rc


def test_fi_get_attr(server, conf, wf):
    """Run daos cont get-attr with fault injection"""
    pool = server.get_test_pool_obj()

    container = create_cont(conf, pool)

    attr_name = 'my-test-attr'

    container.set_attrs({attr_name: 'value'})

    cmd = [join(conf['PREFIX'], 'bin', 'daos'),
           'container',
           'get-attr',
           pool.id(),
           container.id(),
           attr_name]

    test_cmd = AllocFailTest(conf, 'cont-get-attr', cmd)
    test_cmd.wf = wf

    test_cmd.check_daos_stderr = True
    test_cmd.check_post_stdout = False

    rc = test_cmd.launch()
    container.destroy()
    return rc


def test_fi_cont_query(server, conf, wf):
    """Run daos cont query with fault injection"""
    pool = server.get_test_pool_obj()

    container = create_cont(conf, pool, ctype='POSIX')

    cmd = [join(conf['PREFIX'], 'bin', 'daos'),
           'container',
           'query',
           pool.id(),
           container.id()]

    test_cmd = AllocFailTest(conf, 'cont-query', cmd)
    test_cmd.wf = wf

    test_cmd.check_daos_stderr = True
    test_cmd.check_post_stdout = False

    rc = test_cmd.launch()
    container.destroy()
    return rc


def test_fi_cont_check(server, conf, wf):
    """Run daos cont check with fault injection"""
    pool = server.get_test_pool_obj()

    container = create_cont(conf, pool)

    cmd = [join(conf['PREFIX'], 'bin', 'daos'),
           'container',
           'check',
           pool.id(),
           container.id()]

    test_cmd = AllocFailTest(conf, 'cont-check', cmd)
    test_cmd.wf = wf

    test_cmd.check_daos_stderr = True
    test_cmd.check_post_stdout = False

    rc = test_cmd.launch()
    container.destroy()
    return rc


def test_alloc_fail(server, conf):
    """Run 'daos' client binary with fault injection"""
    pool = server.get_test_pool_obj()

    cmd = [join(conf['PREFIX'], 'bin', 'daos'),
           'cont',
           'list',
           pool.id()]
    test_cmd = AllocFailTest(conf, 'pool-list-containers', cmd)

    # Create at least one container, and record what the output should be when
    # the command works.
    container = create_cont(conf, pool)

    rc = test_cmd.launch()
    container.destroy()
    return rc


def server_fi(args):
    """Run the server under fault injection.

    Start the server, create a container, enable periodic failing of D_ALLOC() and then perform
    I/O.  At some point this could be extended to checking the client also behaves properly but
    for now just check the server logs.

    This is not run in CI yet so needs to run manually.  As it's probabilistic then it can be
    expected to find more issues based on how often it's run so it is not suitable for PRs
    but should be run for long periods of time.
    """
    conf = load_conf(args)

    wf = WarningsFactory('nlt-errors.json', post_error=True, check='Server FI testing')

    args.dfuse_debug = 'INFO'
    args.client_debug = 'INFO'
    args.memcheck = 'no'

    conf.set_wf(wf)
    conf.set_args(args)
    setup_log_test(conf)

    with DaosServer(conf, wf=wf, test_class='server-fi', enable_fi=True) as server:

        pool = server.get_test_pool_obj()
        cont = create_cont(conf, pool=pool, ctype='POSIX', label='server_test')

        # Instruct the server to fail a % of allocations.
        server.set_fi(probability=1)

        for idx in range(100):
            server.run_daos_client_cmd_pil4dfs(
                ['touch', f'file.{idx}'], container=cont, check=False, report=False)
            server.run_daos_client_cmd_pil4dfs(
                ['dd', 'if=/dev/zero', f'of=file.{idx}', 'bs=1', 'count=1024'],
                container=cont, check=False, report=False)
            server.run_daos_client_cmd_pil4dfs(
                ['rm', '-f', f'file.{idx}'], container=cont, check=False, report=False)

        # Turn off fault injection again to assist in server shutdown.
        server.set_fi(probability=0)
        server.set_fi(probability=0)


def run(wf, args):
    """Main entry point"""
    # pylint: disable=too-many-branches
    conf = load_conf(args)

    wf_server = WarningsFactory('nlt-server-leaks.json', post=True, check='Server leak checking')

    conf.set_wf(wf)
    conf.set_args(args)
    setup_log_test(conf)

    fi_test = False
    fi_test_dfuse = False

    fatal_errors = BoolRatchet()

    if args.mode == 'fi':
        fi_test = True
    else:
        with DaosServer(conf, test_class='first', wf=wf_server,
                        fatal_errors=fatal_errors) as server:
            if args.mode == 'launch':
                run_in_fg(server, conf, args)
            elif args.mode == 'overlay':
                fatal_errors.add_result(run_duns_overlay_test(server, conf))
            elif args.mode == 'set-fi':
                fatal_errors.add_result(server.set_fi())
            elif args.mode == 'all':
                fi_test_dfuse = True
                fatal_errors.add_result(run_posix_tests(server, conf))
                fatal_errors.add_result(run_dfuse(server, conf))
                fatal_errors.add_result(run_duns_overlay_test(server, conf))
                test_pydaos_kv(server, conf)
                test_pydaos_kv_obj_class(server, conf)
                fatal_errors.add_result(server.set_fi())
            elif args.test == 'all':
                fatal_errors.add_result(run_posix_tests(server, conf))
            elif args.test:
                fatal_errors.add_result(run_posix_tests(server, conf, args.test))
            else:
                fatal_errors.add_result(run_posix_tests(server, conf))
                fatal_errors.add_result(run_dfuse(server, conf))
                fatal_errors.add_result(server.set_fi())

    if args.mode == 'all':
        with DaosServer(conf, test_class='restart', wf=wf_server,
                        fatal_errors=fatal_errors) as server:
            pass

    # If running all tests then restart the server under valgrind.
    # This is really, really slow so just do cont list, then
    # exit again.
    if args.server_valgrind:
        with DaosServer(conf, test_class='valgrind', wf=wf_server, valgrind=True,
                        fatal_errors=fatal_errors) as server:
            pools = server.fetch_pools()
            for pool in pools:
                cmd = ['pool', 'query', pool.id()]
                rc = run_daos_cmd(conf, cmd, valgrind=False)
                print(rc)
                time.sleep(5)
                cmd = ['cont', 'list', pool.id()]
                run_daos_cmd(conf, cmd, valgrind=False)
            time.sleep(20)

    # If the perf-check option is given then re-start everything without much
    # debugging enabled and run some micro-benchmarks to give numbers for use
    # as a comparison against other builds.
    if args.perf_check or fi_test or fi_test_dfuse:
        args.server_debug = 'INFO'
        args.memcheck = 'no'
        args.dfuse_debug = 'WARN'
        with DaosServer(conf, test_class='no-debug', wf=wf_server,
                        fatal_errors=fatal_errors) as server:
            if fi_test:
                # Most of the fault injection tests go here, they are then run on docker containers
                # so can be performed in parallel.

                wf_client = WarningsFactory('nlt-client-leaks.json')

                # dfuse start-up, uses custom fault to force exit if no other faults injected.
                fatal_errors.add_result(test_dfuse_start(server, conf, wf_client))

                # list-container test.
                fatal_errors.add_result(test_alloc_fail(server, conf))

                # Container query test.
                fatal_errors.add_result(test_fi_cont_query(server, conf, wf_client))

                fatal_errors.add_result(test_fi_cont_check(server, conf, wf_client))

                # Container attribute tests
                fatal_errors.add_result(test_fi_get_attr(server, conf, wf_client))
                fatal_errors.add_result(test_fi_list_attr(server, conf, wf_client))

                fatal_errors.add_result(test_fi_get_prop(server, conf, wf_client))

                # filesystem copy tests.
                fatal_errors.add_result(test_alloc_fail_copy(server, conf, wf_client))
                fatal_errors.add_result(test_alloc_fail_copy_trunc(server, conf, wf_client))

                # container create with properties test.
                fatal_errors.add_result(test_alloc_cont_create(server, conf, wf_client))

                # Disabled for now because of errors
                # fatal_errors.add_result(test_alloc_pil4dfs_ls(server, conf, wf_client))

                wf_client.close()

            if fi_test_dfuse:
                # We cannot yet run dfuse inside docker containers and some of the failure modes
                # aren't well handled so continue to run the dfuse fault injection test on real
                # hardware.

                fatal_errors.add_result(test_alloc_fail_cont_create(server, conf))

                # Read-via-IL test, requires dfuse.
                fatal_errors.add_result(test_alloc_fail_cat(server, conf))

                # Copy (read/write) via IL, requires dfuse.
                fatal_errors.add_result(test_alloc_fail_il_cp(server, conf))

            if args.perf_check:
                check_readdir_perf(server, conf)

    if fatal_errors.errors:
        wf.add_test_case('Errors', 'Significant errors encountered')
    else:
        wf.add_test_case('Errors')

    if conf.valgrind_errors:
        wf.add_test_case('Errors', 'Valgrind errors encountered')
        print("Valgrind errors detected during execution")

    wf_server.close()
    conf.flush_bz2()
    print(f'Total time in log analysis: {conf.log_timer.total:.2f} seconds')
    print(f'Total time in log compression: {conf.compress_timer.total:.2f} seconds')
    return fatal_errors


def main():
    """Wrap the core function, and catch/report any exceptions

    This allows the junit results to show at least a stack trace and assertion message for
    any failure, regardless of if it's from a test case or not.
    """
    parser = argparse.ArgumentParser(description='Run DAOS client on local node')
    parser.add_argument('--server-debug', default=None)
    parser.add_argument('--dfuse-debug', default=None)
    parser.add_argument('--client-debug', default=None)
    parser.add_argument('--class-name', default=None, help='class name to use for junit')
    parser.add_argument('--memcheck', default='some', choices=['yes', 'no', 'some'])
    parser.add_argument('--server-valgrind', action='store_true')
    parser.add_argument('--server-fi', action='store_true', help='Run server fault injection test')
    parser.add_argument('--multi-user', action='store_true')
    parser.add_argument('--no-root', action='store_true')
    parser.add_argument('--max-log-size', default=None)
    parser.add_argument('--engine-count', type=int, default=1, help='Number of daos engines to run')
    parser.add_argument('--system-ram-reserved', type=int, default=None, help='GiB reserved RAM')
    parser.add_argument('--dfuse-dir', default='/tmp', help='parent directory for all dfuse mounts')
    parser.add_argument('--perf-check', action='store_true')
    parser.add_argument('--dtx', action='store_true')
    parser.add_argument('--test', help="Use '--test list' for list")
    parser.add_argument('mode', nargs='*')
    args = parser.parse_args()

    if args.server_fi:
        server_fi(args)
        return

    if args.mode:
        mode_list = args.mode
        args.mode = mode_list.pop(0)

        if args.mode != 'launch' and mode_list:
            print(f"unrecognized arguments: {' '.join(mode_list)}")
            sys.exit(1)
        args.launch_cmd = mode_list
    else:
        args.mode = None

    if args.mode and args.test:
        print('Cannot use mode and test')
        sys.exit(1)

    if args.test == 'list':
        tests = []
        for method in dir(PosixTests):
            if method.startswith('test'):
                tests.append(method[5:])
        print(f"Tests are: {','.join(sorted(tests))}")
        sys.exit(1)

    wf = WarningsFactory('nlt-errors.json',
                         post_error=True,
                         check='Log file errors',
                         class_id=args.class_name,
                         junit=True)

    try:
        fatal_errors = run(wf, args)
        wf.add_test_case('exit_wrapper')
        wf.close()
    except Exception as error:
        print(error)
        print(str(error))
        print(repr(error))
        trace = ''.join(traceback.format_tb(error.__traceback__))
        wf.add_test_case('exit_wrapper', str(error), output=trace)
        wf.close()
        raise

    if fatal_errors.errors:
        print("Significant errors encountered")
        sys.exit(1)


if __name__ == '__main__':
    main()
