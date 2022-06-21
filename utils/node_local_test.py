#!/usr/bin/env python3
"""
Node local test (NLT).

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
from collections import OrderedDict
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


instance_num = 0


def get_inc_id():
    """Return a unique character"""
    global instance_num
    instance_num += 1
    return '{:04d}'.format(instance_num)


def umount(path, bg=False):
    """Umount dfuse from a given path"""
    if bg:
        cmd = ['fusermount3', '-uz', path]
    else:
        cmd = ['fusermount3', '-u', path]
    ret = subprocess.run(cmd, check=False)
    print('rc from umount {}'.format(ret.returncode))
    return ret.returncode

class NLTConf():
    """Helper class for configuration"""
    def __init__(self, bc, args):
        self.bc = bc
        self.agent_dir = None
        self.wf = None
        self.args = None
        self.max_log_size = None
        self.valgrind_errors = False
        self.lt = CulmTimer()
        self.lt_compress = CulmTimer()
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
        return self.bc[key]

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
        self.lt_compress.start()
        for proc in self._compress_procs:
            proc.wait()
        self._compress_procs = []
        self.lt_compress.stop()

class CulmTimer():
    """Class to keep track of elapsed time so we know where to focus performance tuning"""

    def __init__(self):
        self.total = 0
        self._start = None

    def start(self):
        """Start the timer"""
        self._start = time.time()

    def stop(self):
        """Stop the timer, and add elapsed to total"""
        self.total += time.time() - self._start

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
            tc = junit_xml.TestCase('Sanity', classname=self._class_name('core'))
            tc.add_error_info('NLT exited abnormally')
            test_case = junit_xml.TestCase('Startup', classname=self._class_name('core'))
            self.ts = junit_xml.TestSuite('Node Local Testing', test_cases=[test_case, tc])
            self._write_test_file()
        else:
            self.ts = None

    def _class_name(self, class_name):
        """Return a formatted ID string for class"""

        if self._class_id:
            return 'NLT.{}.{}'.format(self._class_id, class_name)
        return 'NLT.{}'.format(class_name)

    def __del__(self):
        """Ensure the file is flushed on exit, but if it hasn't already
        been closed then mark an error"""
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
        self.ts = None
        self.close()

    def add_test_case(self, name, failure=None, test_class='core', output=None, duration=None,
                      stdout=None, stderr=None):
        """Add a test case to the results

        class and other metadata will be set automatically,
        if failure is set the test will fail with the message
        provided.  Saves the state to file after each update.
        """
        if not self.ts:
            return

        tc = junit_xml.TestCase(name, classname=self._class_name(test_class), elapsed_sec=duration,
                                stdout=stdout, stderr=stderr)
        if failure:
            tc.add_failure_info(failure, output=output)
        self.ts.test_cases.append(tc)

        self._write_test_file()

    def _write_test_file(self):
        """Write test results to file"""

        with open('nlt-junit.xml', 'w') as f:
            junit_xml.TestSuite.to_file(f, [self.ts], prettyprint=True)

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
            symptoms.add('Process died with signal {}'.format(esignal))
            sev = 'ERROR'
            mtype = 'Fault injection caused crash'
            count += 1

        if count == 0:
            return

        for (sline, smessage) in self.pending:
            locs.add('{}:{}'.format(sline.filename, sline.lineno))
            symptoms.add(smessage)

        preamble = 'Fault injected here caused {} errors,' \
                   ' logfile {}:'.format(count, log_file)

        message = '{} {} {}'.format(preamble,
                                    ' '.join(sorted(symptoms)),
                                    ' '.join(sorted(locs)))
        self.add(line,
                 sev,
                 message,
                 cat='Fault injection location',
                 mtype=mtype)
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
        entry['message'] = '{}\n{}'.format(line.get_anon_msg(), message)
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
            print('::warning file={},line={},::{}, {}'.format(line.filename,
                                                              line.lineno,
                                                              self.check,
                                                              message))

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
        print('Closed JSON file {} with {} errors'.format(self.filename,
                                                          len(self.issues)))
        if self.ts:
            # This is a controlled shutdown, so wipe the error saying forced
            # exit.
            self.ts.test_cases[1].errors = []
            self.ts.test_cases[1].error_message = []
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
            raise Exception('build file not found')
    with open(json_file, 'r') as ofh:
        conf = json.load(ofh)
    return NLTConf(conf, args)

def get_base_env(clean=False):
    """Return the base set of env vars needed for DAOS"""

    if clean:
        env = OrderedDict()
    else:
        env = os.environ.copy()
    env['DD_MASK'] = 'all'
    env['DD_SUBSYS'] = 'all'
    env['D_LOG_MASK'] = 'DEBUG'
    env['D_LOG_SIZE'] = '5g'
    env['FI_UNIVERSE_SIZE'] = '128'
    return env

class DaosPool():
    """Class to store data about daos pools"""
    def __init__(self, server, pool_uuid, label):
        self._server = server
        self.uuid = pool_uuid
        self.label = label

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
            containers.append(DaosCont(cont['uuid'], cont['label']))
        return containers


class DaosCont():
    """Class to store data about daos containers"""

    # pylint: disable=too-few-public-methods

    def __init__(self, cont_uuid, label):
        self.uuid = cont_uuid
        self.label = label

class DaosServer():
    """Manage a DAOS server instance"""

    def __init__(self, conf, test_class=None, valgrind=False, wf=None, fe=None):
        self.running = False
        self._file = __file__.lstrip('./')
        self._sp = None
        self.wf = wf
        self.fe = fe
        self.conf = conf
        if test_class:
            self._test_class = 'Server.{}'.format(test_class)
        else:
            self._test_class = None
        self.valgrind = valgrind
        self._agent = None
        self.engines = conf.args.engine_count
        # pylint: disable=consider-using-with
        self.control_log = tempfile.NamedTemporaryFile(prefix='dnt_control_',
                                                       suffix='.log',
                                                       dir=conf.tmp_dir,
                                                       delete=False)
        self.agent_log = tempfile.NamedTemporaryFile(prefix='dnt_agent_',
                                                     suffix='.log',
                                                     dir=conf.tmp_dir,
                                                     delete=False)
        self.server_logs = []
        for engine in range(self.engines):
            prefix = 'dnt_server_{}_'.format(engine)
            lf = tempfile.NamedTemporaryFile(prefix=prefix,
                                             suffix='.log',
                                             dir=conf.tmp_dir,
                                             delete=False)
            self.server_logs.append(lf)
        self.__process_name = 'daos_engine'
        if self.valgrind:
            self.__process_name = 'valgrind'

        socket_dir = '/tmp/dnt_sockets'
        if not os.path.exists(socket_dir):
            os.mkdir(socket_dir)

        self.agent_dir = tempfile.mkdtemp(prefix='dnt_agent_')

        self._yaml_file = None
        self._io_server_dir = None
        self.test_pool = None
        self.network_interface = None
        self.network_provider = None

        # Detect the number of cores for dfuse and do something sensible, if there are
        # more than 32 on the node then use 12, otherwise use the whole node.
        num_cores = len(os.sched_getaffinity(0))
        if num_cores > 32:
            self.dfuse_cores = 12
        else:
            self.dfuse_cores = None
        self.fuse_procs = []

    def __enter__(self):
        self.start()
        return self

    def __exit__(self, _type, _value, _traceback):
        rc = self.stop(self.wf)
        if rc != 0 and self.fe is not None:
            self.fe.fail()
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
            os.rmdir(self.agent_dir)
        except OSError as error:
            print(os.listdir(self.agent_dir))
            raise error

    def _add_test_case(self, op, failure=None, duration=None):
        """Add a test case to the server instance

        Simply wrapper to automatically add the class
        """
        if not self._test_class:
            return

        self.conf.wf.add_test_case(op,
                                   failure=failure,
                                   duration=duration,
                                   test_class=self._test_class)

    def _check_timing(self, op, start, max_time):
        elapsed = time.time() - start
        if elapsed > max_time:
            res = '{} failed after {:.2f}s (max {:.2f}s)'.format(op, elapsed,
                                                                 max_time)
            self._add_test_case(op, duration=elapsed, failure=res)
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

        if self.valgrind:
            valgrind_args = ['--fair-sched=yes',
                             '--xml=yes',
                             '--xml-file=dnt_server.%p.memcheck.xml',
                             '--num-callers=2',
                             '--leak-check=no',
                             '--keep-stacktraces=none',
                             '--undef-value-errors=no']
            self._io_server_dir = tempfile.TemporaryDirectory(prefix='dnt_io_')

            with open(join(self._io_server_dir.name, 'daos_engine'), 'w') as fd:
                fd.write('#!/bin/sh\n')
                fd.write('export PATH=$REAL_PATH\n')
                fd.write('exec valgrind {} daos_engine "$@"\n'.format(' '.join(valgrind_args)))

            os.chmod(join(self._io_server_dir.name, 'daos_engine'),
                     stat.S_IXUSR | stat.S_IRUSR)

            server_env['REAL_PATH'] = '{}:{}'.format(
                join(self.conf['PREFIX'], 'bin'), server_env['PATH'])
            server_env['PATH'] = '{}:{}'.format(self._io_server_dir.name,
                                                server_env['PATH'])

        daos_server = join(self.conf['PREFIX'], 'bin', 'daos_server')

        self_dir = os.path.dirname(os.path.abspath(__file__))

        # Create a server yaml file.  To do this open and copy the
        # nlt_server.yaml file in the current directory, but overwrite
        # the server log file with a temporary file so that multiple
        # server runs do not overwrite each other.
        with open(join(self_dir, 'nlt_server.yaml'), 'r') as scfd:
            scyaml = yaml.safe_load(scfd)
        if self.conf.args.server_debug:
            scyaml['control_log_mask'] = 'ERROR'
            scyaml['engines'][0]['log_mask'] = self.conf.args.server_debug
        scyaml['control_log_file'] = self.control_log.name

        scyaml['socket_dir'] = self.agent_dir

        for (key, value) in server_env.items():
            scyaml['engines'][0]['env_vars'].append('{}={}'.format(key, value))

        ref_engine = copy.deepcopy(scyaml['engines'][0])
        ref_engine['storage'][0]['scm_size'] = int(ref_engine['storage'][0]['scm_size'] /
                                                   self.engines)
        scyaml['engines'] = []
        # Leave some cores for dfuse, and start the daos server after these.
        if self.dfuse_cores:
            first_core = self.dfuse_cores
        else:
            first_core = 0
        server_port_count = int(server_env['FI_UNIVERSE_SIZE'])
        self.network_interface = ref_engine['fabric_iface']
        self.network_provider = scyaml['provider']
        for idx in range(self.engines):
            engine = copy.deepcopy(ref_engine)
            engine['log_file'] = self.server_logs[idx].name
            engine['first_core'] = first_core + (ref_engine['targets'] * idx)
            engine['fabric_iface_port'] += server_port_count * idx
            engine['storage'][0]['scm_mount'] = '{}_{}'.format(
                ref_engine['storage'][0]['scm_mount'], idx)
            scyaml['engines'].append(engine)
        self._yaml_file = tempfile.NamedTemporaryFile(prefix='nlt-server-config-', suffix='.yaml')
        self._yaml_file.write(yaml.dump(scyaml, encoding='utf-8'))
        self._yaml_file.flush()

        cmd = [daos_server, '--config={}'.format(self._yaml_file.name), 'start', '--insecure']

        if self.conf.args.no_root:
            cmd.append('--recreate-superblocks')

        self._sp = subprocess.Popen(cmd)

        agent_config = join(self_dir, 'nlt_agent.yaml')

        agent_bin = join(self.conf['PREFIX'], 'bin', 'daos_agent')

        agent_cmd = [agent_bin,
                     '--config-path', agent_config,
                     '--insecure',
                     '--runtime_dir', self.agent_dir,
                     '--logfile', self.agent_log.name]

        if not self.conf.args.server_debug:
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
        start = time.time()
        max_start_time = 120

        cmd = ['storage', 'format', '--json']
        while True:
            try:
                rc = self._sp.wait(timeout=0.5)
                print(rc)
                res = 'daos server died waiting for start'
                self._add_test_case('format', failure=res)
                raise Exception(res)
            except subprocess.TimeoutExpired:
                pass
            rc = self.run_dmg(cmd)

            data = json.loads(rc.stdout.decode('utf-8'))
            print('cmd: {} data: {}'.format(cmd, data))

            if data['error'] is None:
                break

            if 'running system' in data['error']:
                break

            self._check_timing('format', start, max_start_time)
        duration = time.time() - start
        self._add_test_case('format', duration=duration)
        print('Format completion in {:.2f} seconds'.format(duration))
        self.running = True

        # Now wait until the system is up, basically the format to happen.
        while True:
            time.sleep(0.5)
            if self._check_system_state(['ready', 'joined']):
                break
            self._check_timing("start", start, max_start_time)
        duration = time.time() - start
        self._add_test_case('start', duration=duration)
        print('Server started in {:.2f} seconds'.format(duration))
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
            status_file = '/proc/{}/status'.format(proc_id)
            if not os.path.exists(status_file):
                continue
            with open(status_file, 'r') as fd:
                for line in fd.readlines():
                    try:
                        key, v = line.split(':', maxsplit=2)
                    except ValueError:
                        continue
                    value = v.strip()
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
            message = 'Incorrect number of engines running ({} vs {})'\
                      .format(len(procs), self.engines)
            entry['message'] = message
            self.conf.wf.issues.append(entry)
            self._add_test_case('server_stop', failure=message)
        rc = self.run_dmg(['system', 'stop'])
        if rc.returncode != 0:
            print(rc)
            entry = {}
            entry['fileName'] = self._file
            # pylint: disable=protected-access
            entry['lineStart'] = sys._getframe().f_lineno
            entry['severity'] = 'ERROR'
            msg = 'dmg system stop failed with {}'.format(rc.returncode)
            entry['message'] = msg
            self.conf.wf.issues.append(entry)
        assert rc.returncode == 0, rc

        start = time.time()
        max_stop_time = 30
        while True:
            time.sleep(0.5)
            if self._check_system_state('stopped'):
                break
            self._check_timing("stop", start, max_stop_time)

        duration = time.time() - start
        self._add_test_case('stop', duration=duration)
        print('Server stopped in {:.2f} seconds'.format(duration))

        self._sp.send_signal(signal.SIGTERM)
        ret = self._sp.wait(timeout=5)
        print('rc from server is {}'.format(ret))

        self.conf.compress_file(self.agent_log.name)
        self.conf.compress_file(self.control_log.name)

        for log in self.server_logs:
            log_test(self.conf, log.name, leak_wf=wf)
            self.server_logs.remove(log)
        self.running = False
        return ret

    def run_dmg(self, cmd):
        """Run the specified dmg command"""

        exe_cmd = [join(self.conf['PREFIX'], 'bin', 'dmg')]
        exe_cmd.append('--insecure')
        exe_cmd.extend(cmd)

        print('running {}'.format(exe_cmd))
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
            size = 1024*2
        else:
            size = 1024*4

        rc = self.run_dmg(['pool',
                           'create',
                           '--label',
                           'NLT',
                           '--scm-size',
                           '{}M'.format(size)])
        print(rc)
        assert rc.returncode == 0
        self.fetch_pools()

    def get_test_pool_obj(self):
        """Return a pool object to be used for testing

        Create a pool as required"""

        if self.test_pool is None:
            self._make_pool()

        return self.test_pool

    def get_test_pool(self):
        """Return a pool uuid to be used for testing

        Create a pool as required"""

        if self.test_pool is None:
            self._make_pool()

        return self.test_pool.uuid

    def get_test_pool_id(self):
        """Return a pool uuid to be used for testing

        Create a pool as required"""

        if self.test_pool is None:
            self._make_pool()

        return self.test_pool.id()

def il_cmd(dfuse, cmd, check_read=True, check_write=True, check_fstat=True):
    """Run a command under the interception library

    Do not run valgrind here, not because it's not useful
    but the options needed are different.  Valgrind handles
    linking differently so some memory is wrongly lost that
    would be freed in the _fini() function, and a lot of
    commands do not free all memory anyway.
    """
    my_env = get_base_env()
    prefix = 'dnt_dfuse_il_{}_'.format(get_inc_id())
    with tempfile.NamedTemporaryFile(prefix=prefix, suffix='.log', delete=False) as log_file:
        log_name = log_file.name
    my_env['D_LOG_FILE'] = log_name
    my_env['LD_PRELOAD'] = join(dfuse.conf['PREFIX'], 'lib64', 'libioil.so')
    # pylint: disable=protected-access
    my_env['DAOS_AGENT_DRPC_DIR'] = dfuse._daos.agent_dir
    my_env['D_IL_REPORT'] = '2'
    ret = subprocess.run(cmd, env=my_env, check=False)
    print('Logged il to {}'.format(log_name))
    print(ret)

    if dfuse.caching:
        check_fstat = False

    try:
        log_test(dfuse.conf, log_name, check_read=check_read, check_write=check_write,
                 check_fstat=check_fstat)
        assert ret.returncode == 0
    except NLTestNoFunction as error:
        print("ERROR: command '{}' did not log via {}".format(' '.join(cmd), error.function))
        ret.returncode = 1

    return ret

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

        self.src_dir = '{}/'.format(os.path.realpath(
            os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

    def get_cmd_prefix(self):
        """Return the command line prefix"""

        if not self.use_valgrind:
            return []

        if not self._logid:
            self._logid = get_inc_id()

        with tempfile.NamedTemporaryFile(prefix='dnt.{}.'.format(self._logid), dir='.',
                                         suffix='.memcheck', delete=False) as log_file:
            self._xml_file = log_file.name

        cmd = ['valgrind', '--fair-sched=yes']

        if self.full_check:
            cmd.extend(['--leak-check=full', '--show-leak-kinds=all'])
        else:
            cmd.append('--leak-check=no')

        cmd.append('--gen-suppressions=all')

        src_suppression_file = join('src', 'cart', 'utils', 'memcheck-cart.supp')
        if os.path.exists(src_suppression_file):
            cmd.append('--suppressions={}'.format(src_suppression_file))
        else:
            cmd.append('--suppressions={}'.format(
                join(self.conf['PREFIX'], 'etc', 'memcheck-cart.supp')))

        cmd.append('--error-exitcode=42')

        cmd.extend(['--xml=yes', '--xml-file={}'.format(self._xml_file)])
        return cmd

    def convert_xml(self):
        """Modify the xml file"""

        if not self.use_valgrind:
            return
        with open(self._xml_file, 'r') as fd:
            with open('{}.xml'.format(self._xml_file), 'w') as ofd:
                for line in fd:
                    if self.src_dir in line:
                        ofd.write(line.replace(self.src_dir, ''))
                    else:
                        ofd.write(line)
        os.unlink(self._xml_file)

class DFuse():
    """Manage a dfuse instance"""

    instance_num = 0

    def __init__(self,
                 daos,
                 conf,
                 pool=None,
                 container=None,
                 mount_path=None,
                 uns_path=None,
                 caching=True,
                 wbcache=True):
        if mount_path:
            self.dir = mount_path
        else:
            self.dir = tempfile.mkdtemp(dir=conf.dfuse_parent_dir, prefix='dfuse_mount.')
        self.pool = pool
        self.uns_path = uns_path
        self.container = container
        self.conf = conf
        self.cores = daos.dfuse_cores
        self._daos = daos
        self.caching = caching
        self.wbcache = wbcache
        self.use_valgrind = True
        self._sp = None
        self.log_flush = False

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

    def start(self, v_hint=None, single_threaded=False):
        """Start a dfuse instance"""

        # pylint: disable=consider-using-with
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
        log_file = tempfile.NamedTemporaryFile(prefix=prefix,
                                               suffix='.log',
                                               delete=False)
        self.log_file = log_file.name

        my_env['D_LOG_FILE'] = self.log_file
        my_env['DAOS_AGENT_DRPC_DIR'] = self._daos.agent_dir
        if self.conf.args.dtx == 'yes':
            my_env['DFS_USE_DTX'] = '1'

        self.valgrind = ValgrindHelper(self.conf, v_hint)
        if self.conf.args.memcheck == 'no':
            self.valgrind.use_valgrind = False

        if not self.use_valgrind:
            self.valgrind.use_valgrind = False

        if self.cores:
            cmd = ['numactl', '--physcpubind', '0-{}'.format(self.cores - 1)]
        else:
            cmd = []

        cmd.extend(self.valgrind.get_cmd_prefix())

        cmd.extend([dfuse_bin,
                    '--mountpoint',
                    self.dir,
                    '--foreground'])

        if single_threaded:
            cmd.append('--singlethread')

        if not self.caching:
            cmd.append('--disable-caching')
        else:
            if not self.wbcache:
                cmd.append('--disable-wb-cache')

        if self.uns_path:
            cmd.extend(['--path', self.uns_path])

        if self.pool:
            cmd.extend(['--pool', self.pool])
        if self.container:
            cmd.extend(['--container', self.container])
        print('Running {}'.format(' '.join(cmd)))
        self._sp = subprocess.Popen(cmd, env=my_env)
        print('Started dfuse at {}'.format(self.dir))
        print('Log file is {}'.format(self.log_file))

        total_time = 0
        while os.stat(self.dir).st_ino == pre_inode:
            print('Dfuse not started, waiting...')
            try:
                ret = self._sp.wait(timeout=1)
                print('dfuse command exited with {}'.format(ret))
                self._sp = None
                if os.path.exists(self.log_file):
                    log_test(self.conf, self.log_file)
                os.rmdir(self.dir)
                raise Exception('dfuse died waiting for start')
            except subprocess.TimeoutExpired:
                pass
            total_time += 1
            if total_time > 60:
                raise Exception('Timeout starting dfuse')

        self._daos.add_fuse(self)

    def _close_files(self):
        work_done = False
        for fname in os.listdir('/proc/self/fd'):
            try:
                tfile = os.readlink(join('/proc/self/fd', fname))
            except FileNotFoundError:
                continue
            if tfile.startswith(self.dir):
                print('closing file {}'.format(tfile))
                os.close(int(fname))
                work_done = True
        return work_done

    def __del__(self):
        if self._sp:
            self.stop()

    def stop(self):
        """Stop a previously started dfuse instance"""

        fatal_errors = False
        if not self._sp:
            return fatal_errors

        print('Stopping fuse')
        ret = umount(self.dir)
        if ret:
            umount(self.dir, bg=True)
            self._close_files()
            time.sleep(2)
            umount(self.dir)

        run_log_test = True
        try:
            ret = self._sp.wait(timeout=20)
            print('rc from dfuse {}'.format(ret))
            if ret == 42:
                self.conf.wf.add_test_case(str(self), failure='valgrind errors', output=ret)
                self.conf.valgrind_errors = True
            elif ret != 0:
                fatal_errors = True
        except subprocess.TimeoutExpired:
            print('Timeout stopping dfuse')
            self._sp.send_signal(signal.SIGTERM)
            fatal_errors = True
            run_log_test = False
        self._sp = None
        if run_log_test:
            log_test(self.conf, self.log_file)

        # Finally, modify the valgrind xml file to remove the
        # prefix to the src dir.
        self.valgrind.convert_xml()
        os.rmdir(self.dir)
        self._daos.remove_fuse(self)
        return fatal_errors

    def wait_for_exit(self):
        """Wait for dfuse to exit"""
        ret = self._sp.wait()
        print('rc from dfuse {}'.format(ret))
        self._sp = None
        log_test(self.conf, self.log_file)

        # Finally, modify the valgrind xml file to remove the
        # prefix to the src dir.
        self.valgrind.convert_xml()
        os.rmdir(self.dir)

def assert_file_size_fd(fd, size):
    """Verify the file size is as expected"""
    my_stat = os.fstat(fd)
    print('Checking file size is {} {}'.format(size, my_stat.st_size))
    assert my_stat.st_size == size

def assert_file_size(ofd, size):
    """Verify the file size is as expected"""
    assert_file_size_fd(ofd.fileno(), size)

def import_daos(server, conf):
    """Return a handle to the pydaos module"""

    pydir = 'python{}.{}'.format(sys.version_info.major, sys.version_info.minor)

    sys.path.append(join(conf['PREFIX'], 'lib64', pydir, 'site-packages'))

    os.environ['DD_MASK'] = 'all'
    os.environ['DD_SUBSYS'] = 'all'
    os.environ['D_LOG_MASK'] = 'DEBUG'
    os.environ['FI_UNIVERSE_SIZE'] = '128'
    os.environ['DAOS_AGENT_DRPC_DIR'] = server.agent_dir

    daos = __import__('pydaos')
    return daos

class daos_cmd_return():
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
        output = "CompletedDaosCommand(cmd='{}')".format(' '.join(self.cmd))
        output += '\nReturncode is {}'.format(self.rc.returncode)
        if self.valgrind:
            output += "\nProcess ran under valgrind with '{}'".format(' '.join(self.valgrind))

        try:
            pp = pprint.PrettyPrinter()
            output += '\njson output:\n' + pp.pformat(self.rc.json)
        except AttributeError:
            for line in self.rc.stdout.splitlines():
                output += '\nstdout: {}'.format(line)

        for line in self.rc.stderr.splitlines():
            output += '\nstderr: {}'.format(line)
        return output

def run_daos_cmd(conf,
                 cmd,
                 show_stdout=False,
                 valgrind=True,
                 log_check=True,
                 use_json=False):
    """Run a DAOS command

    Run a command, returning what subprocess.run() would.

    Enable logging, and valgrind for the command.

    if prefix is set to False do not run a DAOS command, but instead run what's
    provided, however run it under the IL.
    """

    dcr = daos_cmd_return()
    vh = ValgrindHelper(conf)

    if conf.args.memcheck == 'no':
        valgrind = False

    if not valgrind:
        vh.use_valgrind = False

    exec_cmd = vh.get_cmd_prefix()
    dcr.valgrind = list(exec_cmd)
    daos_cmd = [join(conf['PREFIX'], 'bin', 'daos')]
    if use_json:
        daos_cmd.append('--json')
    daos_cmd.extend(cmd)
    dcr.cmd = daos_cmd
    exec_cmd.extend(daos_cmd)

    cmd_env = get_base_env()
    if not log_check:
        del cmd_env['DD_MASK']
        del cmd_env['DD_SUBSYS']
        del cmd_env['D_LOG_MASK']

    with tempfile.NamedTemporaryFile(prefix='dnt_cmd_{}_'.format(get_inc_id()),
                                     suffix='.log',
                                     dir=conf.tmp_dir,
                                     delete=False) as lf:
        log_name = lf.name
        cmd_env['D_LOG_FILE'] = log_name

    cmd_env['DAOS_AGENT_DRPC_DIR'] = conf.agent_dir

    rc = subprocess.run(exec_cmd,
                        stdout=subprocess.PIPE,
                        stderr=subprocess.PIPE,
                        env=cmd_env,
                        check=False)

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

    rc.fi_loc = log_test(conf, log_name, show_memleaks=show_memleaks)
    vh.convert_xml()
    # If there are valgrind errors here then mark them for later reporting but
    # do not abort.  This allows a full-test run to report all valgrind issues
    # in a single test run.
    if vh.use_valgrind and rc.returncode == 42:
        print("Valgrind errors detected")
        print(rc)
        conf.wf.add_test_case(' '.join(cmd), failure='valgrind errors', output=rc)
        conf.valgrind_errors = True
        rc.returncode = 0
    if use_json:
        rc.json = json.loads(rc.stdout.decode('utf-8'))
    dcr.rc = rc
    return dcr

def create_cont(conf,
                pool=None,
                ctype=None,
                label=None,
                path=None,
                valgrind=False,
                log_check=True):
    """Create a container and return the uuid"""

    print("DBG> -------------------")
    for line in traceback.format_stack():
        print(line.strip())
    cmd = ['container', 'create']

    if pool:
        cmd.append(pool)

    if label:
        cmd.extend(['--properties', 'label:{}'.format(label)])

    if path:
        cmd.extend(['--path', path])
        ctype = 'POSIX'

    if ctype:
        cmd.extend(['--type', ctype])

    def _create_cont():
        """Helper function for create_cont"""

        rc = run_daos_cmd(conf, cmd, use_json=True, log_check=log_check, valgrind=valgrind)
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
        if 'container_label' in rc.json['response'].keys():
            print('DBG> Error!!!')
            for line in traceback.format_stack():
                print(line.strip())
        assert 'container_label' not in rc.json['response'].keys()
    return rc.json['response']['container_uuid']

def destroy_container(conf, pool, container, valgrind=True, log_check=True):
    """Destroy a container"""
    cmd = ['container', 'destroy', pool, container]
    rc = run_daos_cmd(conf, cmd, valgrind=valgrind, use_json=True, log_check=log_check)
    print(rc)
    if rc.returncode == 1 and rc.json['status'] == -1012:
        # This shouldn't happen but can on unclean shutdown, file it as a test failure so it does
        # not get lost, however destroy the container and attempt to continue.
        # DAOS-8860
        conf.wf.add_test_case('destroy_container_{}/{}'.format(pool, container),
                              failure='Failed to destroy container',
                              output=rc)
        cmd = ['container', 'destroy', '--force', pool, container]
        rc = run_daos_cmd(conf, cmd, valgrind=valgrind, use_json=True)
        print(rc)
    assert rc.returncode == 0, rc

def check_dfs_tool_output(output, oclass, csize):
    """verify daos fs tool output"""
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
            self.test_name = '{}_with_caching'.format(method.__name__)
        else:
            caching = False

        self.dfuse = DFuse(self.server,
                           self.conf,
                           caching=caching,
                           pool=self.pool.dfuse_mount_name(),
                           container=self.container_label)
        self.dfuse.start(v_hint=self.test_name)
        try:
            rc = method(self)
        finally:
            if self.dfuse.stop():
                self.fatal_errors = True
        return rc

    return _helper

class needs_dfuse_with_opt():
    """Decorator class for starting dfuse under posix_tests class

    By default runs the method twice, once with caching and once without, however can be
    configured to behave differently.  Interacts with the run_posix_tests._run_test() method
    to achieve this.
    """

    # pylint: disable=too-few-public-methods

    def __init__(self, caching=None, single_threaded=False):
        self.caching = caching
        self.single_threaded = single_threaded

    def __call__(self, method):

        @functools.wraps(method)
        def _helper(obj):

            caching = self.caching
            if caching is None:
                if obj.call_index == 0:
                    caching = True
                    obj.needs_more = True
                    obj.test_name = '{}_with_caching'.format(method.__name__)
                else:
                    caching = False

            obj.dfuse = DFuse(obj.server,
                              obj.conf,
                              caching=caching,
                              pool=obj.pool.dfuse_mount_name(),
                              container=obj.container)
            obj.dfuse.start(v_hint=method.__name__, single_threaded=self.single_threaded)
            try:
                rc = method(obj)
            finally:
                if obj.dfuse.stop():
                    obj.fatal_errors = True
            return rc
        return _helper

class print_stat():
    """Class for nicely showing file 'stat' data, similar to ls -l"""

    headers = ['uid', 'gid', 'size', 'mode', 'filename']

    def __init__(self, filename=None):
        # Setup the object, and maybe add some data to it.
        self._stats = []
        if filename:
            self.add(filename)

    def add(self, filename, attr=None, show_dir=False):
        """Add an entry to be displayed"""

        if attr is None:
            attr = os.stat(filename)

        self._stats.append([attr.st_uid,
                            attr.st_gid,
                            attr.st_size,
                            stat.filemode(attr.st_mode),
                            filename])

        if show_dir:
            tab = '.' * len(filename)
            for fname in os.listdir(filename):
                self.add(join(tab, fname), attr=os.stat(join(filename, fname)))

    def __str__(self):
        return tabulate.tabulate(self._stats, self.headers)

# This is test code where methods are tests, so we want to have lots of them.

class posix_tests():
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

        print('Comparing real vs expected contents of {}'.format(dir_name))
        print('expected: "{}"'.format(','.join(expected)))
        print('actual:   "{}"'.format(','.join(files)))

        assert files == expected

    def test_cont_list(self):
        """Test daos container list"""

        rc = run_daos_cmd(self.conf, ['container', 'list', self.pool.id()])
        print(rc)
        assert rc.returncode == 0, rc

    def test_cache(self):
        """Test with caching enabled"""

        container = create_cont(self.conf, self.pool.id(), ctype="POSIX", label='Cache')
        run_daos_cmd(self.conf,
                     ['container', 'query',
                      self.pool.id(), container],
                     show_stdout=True)

        run_daos_cmd(self.conf,
                     ['container', 'set-attr',
                      self.pool.id(), container,
                      '--attr', 'dfuse-attr-time', '--value', '2'],
                     show_stdout=True)

        run_daos_cmd(self.conf,
                     ['container', 'set-attr',
                      self.pool.id(), container,
                      '--attr', 'dfuse-dentry-time', '--value', '100s'],
                     show_stdout=True)

        run_daos_cmd(self.conf,
                     ['container', 'set-attr',
                      self.pool.id(), container,
                      '--attr', 'dfuse-dentry-time-dir', '--value', '100s'],
                     show_stdout=True)

        run_daos_cmd(self.conf,
                     ['container', 'set-attr',
                      self.pool.id(), container,
                      '--attr', 'dfuse-ndentry-time', '--value', '100s'],
                     show_stdout=True)

        run_daos_cmd(self.conf,
                     ['container', 'list-attrs',
                      self.pool.id(), container],
                     show_stdout=True)

        dfuse = DFuse(self.server,
                      self.conf,
                      pool=self.pool.uuid,
                      container=container)
        dfuse.start()

        print(os.listdir(dfuse.dir))

        if dfuse.stop():
            self.fatal_errors = True

        destroy_container(self.conf, self.pool.id(), container)

    @needs_dfuse
    def test_truncate(self):
        """Test file read after truncate"""

        filename = join(self.dfuse.dir, 'myfile')

        with open(filename, 'w') as fd:
            fd.write('hello')

        os.truncate(filename, 1024*1024*4)
        with open(filename, 'r') as fd:
            data = fd.read(5)
            print('_{}_'.format(data))
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
        new_cont = create_cont(self.conf, self.pool.uuid, path=child_path)
        print(new_cont)
        _check_cmd(child_path)
        _check_cmd(self.dfuse.dir)

        # Do not destroy the container at this point as dfuse will be holding a reference to it.
        # destroy_container(self.conf, self.pool.id(), new_cont)

    def test_two_mounts(self):
        """Create two mounts, and check that a file created in one
        can be read from the other"""

        dfuse0 = DFuse(self.server,
                       self.conf,
                       caching=False,
                       pool=self.pool.uuid,
                       container=self.container)
        dfuse0.start(v_hint='two_0')

        dfuse1 = DFuse(self.server,
                       self.conf,
                       caching=True,
                       pool=self.pool.uuid,
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

    @needs_dfuse
    def test_readdir_25(self):
        """Test reading a directory with 25 entries"""
        self.readdir_test(25, test_all=True)

    # Works, but is very slow so needs to be run without debugging.
    #@needs_dfuse
    #def test_readdir_300(self):
    #    self.readdir_test(300, test_all=False)

    def readdir_test(self, count, test_all=False):
        """Run a rudimentary readdir test"""

        wide_dir = tempfile.mkdtemp(dir=self.dfuse.dir)
        if count == 0:
            files = os.listdir(wide_dir)
            assert len(files) == 0
            return
        start = time.time()
        for idx in range(count):
            with open(join(wide_dir, str(idx)), 'w'):
                pass
            if test_all:
                files = os.listdir(wide_dir)
                assert len(files) == idx + 1
        duration = time.time() - start
        rate = count / duration
        print('Created {} files in {:.1f} seconds rate {:.1f}'.format(count,
                                                                      duration,
                                                                      rate))
        print('Listing dir contents')
        start = time.time()
        files = os.listdir(wide_dir)
        duration = time.time() - start
        rate = count / duration
        print('Listed {} files in {:.1f} seconds rate {:.1f}'.format(count,
                                                                     duration,
                                                                     rate))
        print(files)
        print(len(files))
        assert len(files) == count

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

        rc = il_cmd(self.dfuse,
                    ['cat', fname],
                    check_write=False,
                    check_fstat=check_fstat)
        assert rc.returncode == 0

    @needs_dfuse_with_opt(caching=False)
    def test_il(self):
        """Run a basic interception library test"""

        # Sometimes the write can be cached in the kernel and the cp will not read any data so
        # do not run this test with caching on.

        create_and_read_via_il(self.dfuse, self.dfuse.dir)

        sub_cont_dir = join(self.dfuse.dir, 'child')
        create_cont(self.conf, path=sub_cont_dir)

        # Create a file natively.
        f = join(self.dfuse.dir, 'file')
        with open(f, 'w') as fd:
            fd.write('Hello')
        # Copy it across containers.
        ret = il_cmd(self.dfuse, ['cp', f, sub_cont_dir])
        assert ret.returncode == 0

        # Copy it within the container.
        child_dir = join(self.dfuse.dir, 'new_dir')
        os.mkdir(child_dir)
        il_cmd(self.dfuse, ['cp', f, child_dir])
        assert ret.returncode == 0

        # Copy something into a container
        ret = il_cmd(self.dfuse, ['cp', '/bin/bash', sub_cont_dir], check_read=False)
        assert ret.returncode == 0
        # Read it from within a container
        ret = il_cmd(self.dfuse, ['md5sum', join(sub_cont_dir, 'bash')],
                     check_read=False, check_write=False, check_fstat=False)
        assert ret.returncode == 0
        ret = il_cmd(self.dfuse, ['dd',
                                  'if={}'.format(join(sub_cont_dir, 'bash')),
                                  'of={}'.format(join(sub_cont_dir, 'bash_copy')),
                                  'iflag=direct',
                                  'oflag=direct',
                                  'bs=128k'],
                     check_fstat=False)
        assert ret.returncode == 0

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
                print('xattr is {}:{}'.format(key, value))

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
        nf = os.stat(fname)
        assert stat.S_IMODE(nf.st_mode) == e_mode

    @needs_dfuse
    def test_uns_create(self):
        """Simple test to create a container using a path in dfuse"""
        path = join(self.dfuse.dir, 'mycont')
        create_cont(self.conf, path=path)
        stbuf = os.stat(path)
        print(stbuf)
        assert stbuf.st_ino < 100
        print(os.listdir(path))

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
            with open(join(self.dfuse.dir, 'file.{}'.format(index)), 'w') as fd:
                fd.write('test')

        # Start another dfuse instance to move the files around without the kernel knowing.
        dfuse = DFuse(self.server,
                      self.conf,
                      pool=self.pool.id(),
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
                    with open(join(root, 'file.{}'.format(index)), 'w') as fd:
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
                    os.mkdir(join(root, 'dir.{}'.format(index)))
                os.rename('dir.0', 'dir.1', src_dir_fd=dfd, dst_dir_fd=dfd)
                self._check_dirs_equal(['dir.1'], root)
                self._check_dirs_equal([], join(root, 'dir.1'))
                os.rmdir(join(root, 'dir.1'))
                for index in range(2):
                    with open(join(root, 'file.{}'.format(index)), 'w') as fd:
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
                except OSError as e:
                    assert e.errno == errno.ENOTEMPTY
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
            fd = open(join(self.dfuse.dir, 'file.{}'.format(index)), 'wb', buffering=0)
            fd.write(b'test')
            fds.append(fd)

        # Start another dfuse instance to move the files around without the kernel knowing.
        dfuse = DFuse(self.server,
                      self.conf,
                      pool=self.pool.id(),
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
                      pool=self.pool.id(),
                      container=self.container,
                      caching=False)

        dfuse.start(v_hint='cont_rw_1')

        ps = print_stat(dfuse.dir)
        testfile = join(dfuse.dir, 'testfile')
        with open(testfile, 'w') as fd:
            ps.add(testfile, attr=os.fstat(fd.fileno()))

        dirname = join(dfuse.dir, 'rw_dir')
        os.mkdir(dirname)

        ps.add(dirname)

        dir_perms = os.stat(dirname).st_mode
        base_perms = stat.S_IMODE(dir_perms)

        os.chmod(dirname, base_perms | stat.S_IWGRP | stat.S_IXGRP | stat.S_IXOTH | stat.S_IWOTH)
        ps.add(dirname)
        print(ps)

        if dfuse.stop():
            self.fatal_errors = True

            # Update container ACLs so current user has rw permissions only, the minimum required.
        rc = run_daos_cmd(self.conf, ['container',
                                      'update-acl',
                                      self.pool.id(),
                                      self.container,
                                      '--entry',
                                      'A::{}@:rwta'.format(os.getlogin())])
        print(rc)

        # Assign the container to someone else.
        rc = run_daos_cmd(self.conf, ['container',
                                      'set-owner',
                                      self.pool.id(),
                                      self.container,
                                      '--user',
                                      'root@',
                                      '--group',
                                      'root@'])
        print(rc)

        # Now start dfuse and access the container, see who the file is owned by.
        dfuse = DFuse(self.server,
                      self.conf,
                      pool=self.pool.id(),
                      container=self.container,
                      caching=False)
        dfuse.start(v_hint='cont_rw_2')

        ps = print_stat()
        ps.add(dfuse.dir, show_dir=True)

        with open(join(dfuse.dir, 'testfile'), 'r') as fd:
            ps.add(join(dfuse.dir, 'testfile'), os.fstat(fd.fileno()))

        dirname = join(dfuse.dir, 'rw_dir')
        testfile = join(dirname, 'new_file')
        fd = os.open(testfile, os.O_RDWR | os.O_CREAT, mode=int('600', base=8))
        os.write(fd, b'read-only-data')
        ps.add(testfile, attr=os.fstat(fd))
        os.close(fd)
        print(ps)

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
        """Test for rename semantics, and that rename is correctly updating the dfuse data for
        the moved rile.

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
                          pool=self.pool.id(),
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
                                      self.container,
                                      '--entry',
                                      'A::{}@:rta'.format(os.getlogin())])
        print(rc)
        assert rc.returncode == 0

        # Assign the container to someone else.
        rc = run_daos_cmd(self.conf, ['container',
                                      'set-owner',
                                      self.pool.id(),
                                      self.container,
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

        DAOS-6238"""

        path = self.dfuse.dir
        fname = join(path, 'test_file1')
        ofd = os.open(fname, os.O_CREAT | os.O_RDONLY | os.O_EXCL)
        ns = os.stat(fname)
        print(ns)
        os.close(ofd)
        os.chmod(fname, stat.S_IRUSR)
        ns = os.stat(fname)
        print(ns)
        assert stat.S_IMODE(ns.st_mode) == stat.S_IRUSR

        fname = join(path, 'test_file2')
        ofd = os.open(fname, os.O_CREAT | os.O_RDONLY | os.O_EXCL)
        ns = os.stat(fname)
        print(ns)
        os.fchmod(ofd, stat.S_IRUSR)
        os.close(ofd)
        ns = os.stat(fname)
        print(ns)
        assert stat.S_IMODE(ns.st_mode) == stat.S_IRUSR

    def test_with_path(self):
        """Test that dfuse starts with path option."""

        tmp_dir = tempfile.mkdtemp()

        cont_path = join(tmp_dir, 'my-cont')
        create_cont(self.conf, self.pool.uuid, path=cont_path)

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
        dfuse = DFuse(server, conf, pool=pool, container=container,
                      caching=False)
        dfuse.start('uns-0')

        # Create a new container within it using UNS
        uns_path = join(dfuse.dir, 'ep0')
        print('Inserting entry point')
        uns_container = create_cont(conf, pool=pool, path=uns_path)
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
        print(os.listdir(join(dfuse.dir, pool, container)))

        # Now create a UNS link from the 2nd container to a 3rd one.
        uns_path = join(dfuse.dir, pool, container, 'ep0', 'ep')
        second_path = join(dfuse.dir, pool, uns_container)

        # Make a link within the new container.
        print('Inserting entry point')
        uns_container_2 = create_cont(conf, pool=pool, path=uns_path)

        # List the root container again.
        print(os.listdir(join(dfuse.dir, pool, container)))

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

        third_path = join(dfuse.dir, pool, uns_container_2)
        third_stat = os.stat(third_path)
        print(third_stat)
        assert third_stat.st_ino == direct_stat.st_ino

        if dfuse.stop():
            self.fatal_errors = True
        print('Trying UNS with previous cont')
        dfuse = DFuse(server, conf, caching=False)
        dfuse.start('uns-3')

        second_path = join(dfuse.dir, pool, uns_container)
        uns_path = join(dfuse.dir, pool, container, 'ep0', 'ep')
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
        """Test for dfuse with no caching options, but
        direct-io disabled"""

        run_daos_cmd(self.conf,
                     ['container', 'set-attr',
                      self.pool.id(), self.container,
                      '--attr', 'dfuse-direct-io-disable', '--value', 'on'],
                     show_stdout=True)
        dfuse = DFuse(self.server,
                      self.conf,
                      caching=True,
                      pool=self.pool.uuid,
                      container=self.container)

        dfuse.start(v_hint='dio_off')

        print(os.listdir(dfuse.dir))

        fname = join(dfuse.dir, 'test_file3')
        with open(fname, 'w') as ofd:
            ofd.write('hello')

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
        uns_container = create_cont(conf, pool=pool, path=uns_path)

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
        print('rc is {}'.format(rc))
        output = rc.stdout.decode('utf-8')
        assert check_dfs_tool_output(output, 'S1', '1048576')

        # run same command using pool, container, dfs-path, and dfs-prefix
        cmd = ['fs', 'get-attr', '--pool', pool, '--cont', uns_container, '--dfs-path', dir1,
               '--dfs-prefix', uns_path]
        print('get-attr of d1')
        rc = run_daos_cmd(conf, cmd)
        assert rc.returncode == 0
        print('rc is {}'.format(rc))
        output = rc.stdout.decode('utf-8')
        assert check_dfs_tool_output(output, 'S1', '1048576')

        # run same command using pool, container, dfs-path
        cmd = ['fs', 'get-attr', '--pool', pool, '--cont', uns_container, '--dfs-path', '/d1']
        print('get-attr of d1')
        rc = run_daos_cmd(conf, cmd)
        assert rc.returncode == 0
        print('rc is {}'.format(rc))
        output = rc.stdout.decode('utf-8')
        assert check_dfs_tool_output(output, 'S1', '1048576')

        cmd = ['fs', 'get-attr', '--path', file1]
        print('get-attr of d1/f1')
        rc = run_daos_cmd(conf, cmd)
        assert rc.returncode == 0
        print('rc is {}'.format(rc))
        output = rc.stdout.decode('utf-8')
        # SX is not deterministic, so don't check it here
        assert check_dfs_tool_output(output, None, '1048576')

        # Run a command to change attr of dir1
        cmd = ['fs', 'set-attr', '--path', dir1, '--oclass', 'S2',
               '--chunk-size', '16']
        print('set-attr of d1')
        rc = run_daos_cmd(conf, cmd)
        assert rc.returncode == 0
        print('rc is {}'.format(rc))

        # Run a command to change attr of file1, should fail
        cmd = ['fs', 'set-attr', '--path', file1, '--oclass', 'S2',
               '--chunk-size', '16']
        print('set-attr of f1')
        rc = run_daos_cmd(conf, cmd)
        print('rc is {}'.format(rc))
        assert rc.returncode != 0

        # Run a command to create new file with set-attr
        file2 = join(dir1, 'f2')
        cmd = ['fs', 'set-attr', '--path', file2, '--oclass', 'S1']
        print('set-attr of f2')
        rc = run_daos_cmd(conf, cmd)
        assert rc.returncode == 0
        print('rc is {}'.format(rc))

        # Run a command to get attr of dir and file2
        cmd = ['fs', 'get-attr', '--path', dir1]
        print('get-attr of d1')
        rc = run_daos_cmd(conf, cmd)
        assert rc.returncode == 0
        print('rc is {}'.format(rc))
        output = rc.stdout.decode('utf-8')
        assert check_dfs_tool_output(output, 'S2', '16')

        cmd = ['fs', 'get-attr', '--path', file2]
        print('get-attr of d1/f2')
        rc = run_daos_cmd(conf, cmd)
        assert rc.returncode == 0
        print('rc is {}'.format(rc))
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
               'daos://{}/{}'.format(self.pool.uuid, self.container)]
        rc = run_daos_cmd(self.conf, cmd)
        print(rc)
        lineresult = rc.stdout.decode('utf-8').splitlines()
        assert len(lineresult) == 4
        assert lineresult[1] == '    Directories: 1'
        assert lineresult[2] == '    Files:       1'
        assert lineresult[3] == '    Links:       1'
        assert rc.returncode == 0

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
               'daos://{}/{}'.format(self.pool.uuid, self.container)]
        rc = run_daos_cmd(self.conf, cmd)
        print(rc)
        assert rc.returncode == 0

        # Now create a container uuid and do an object based copy.
        # The daos command will create the target container on demand.
        cmd = ['container',
               'clone',
               '--src',
               'daos://{}/{}'.format(self.pool.uuid, self.container),
               '--dst',
               'daos://{}/'.format(self.pool.uuid)]
        rc = run_daos_cmd(self.conf, cmd)
        print(rc)
        assert rc.returncode == 0
        lineresult = rc.stdout.decode('utf-8').splitlines()
        assert len(lineresult) == 2
        destroy_container(self.conf, self.pool.id(), lineresult[1][-36:])

class nlt_stdout_wrapper():
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

class nlt_stderr_wrapper():
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
            start = time.time()
            out_wrapper.sprint('Calling {}'.format(function))
            print('Calling {}'.format(function))

            # Do this with valgrind disabled as this code is run often and valgrind has a big
            # performance impact.  There are other tests that run with valgrind enabled so this
            # should not reduce coverage.
            try:
                ptl.container = create_cont(conf,
                                            pool.id(),
                                            ctype="POSIX",
                                            valgrind=False,
                                            log_check=False,
                                            label=function)
                ptl.container_label = function
                test_cb()
                destroy_container(conf, pool.id(),
                                  ptl.container_label,
                                  valgrind=False,
                                  log_check=False)
                ptl.container = None
            except Exception as inst:
                trace = ''.join(traceback.format_tb(inst.__traceback__))
                duration = time.time() - start
                out_wrapper.sprint('{} Failed'.format(ptl.test_name))
                conf.wf.add_test_case(ptl.test_name,
                                      repr(inst),
                                      stdout=out_wrapper.get_thread_output(),
                                      stderr=err_wrapper.get_thread_err(),
                                      output=trace,
                                      test_class='test',
                                      duration=duration)
                raise
            duration = time.time() - start
            out_wrapper.sprint('Test {} took {:.1f} seconds'.format(ptl.test_name, duration))
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

    server.get_test_pool()
    pool = server.test_pool

    out_wrapper = nlt_stdout_wrapper()
    err_wrapper = nlt_stderr_wrapper()

    pto = posix_tests(server, conf, pool=pool)
    if test:
        fn = 'test_{}'.format(test)
        obj = getattr(pto, fn)

        _run_test(ptl=pto, test_cb=obj, function=fn)
    else:

        threads = []

        slow_tests = ['test_readdir_25', 'test_uns_basic', 'test_daos_fs_tool']

        tests = dir(pto)
        tests.sort(key=lambda x: x not in slow_tests)

        for fn in tests:
            if not fn.startswith('test_'):
                continue

            ptl = posix_tests(server, conf, pool=pool)
            obj = getattr(ptl, fn)
            if not callable(obj):
                continue

            thread = threading.Thread(None,
                                      target=_run_test,
                                      name='test {}'.format(fn),
                                      kwargs={'ptl': ptl, 'test_cb': obj, 'function': fn},
                                      daemon=True)
            thread.start()
            threads.append(thread)

            # Limit the number of concurrent tests, but poll all active threads so there's no
            # expectation for them to complete in order.  At the minute we only have a handful of
            # long-running tests which dominate the time, so whilst a higher value here would
            # work there's no benefit in rushing to finish the quicker tests.  The long-running
            # tests are started first.
            while len(threads) > 5:
                for td in threads:
                    td.join(timeout=0)
                    if td.is_alive():
                        continue
                    threads.remove(td)

        for td in threads:
            td.join()

    # Now check for running dfuse instances, there should be none at this point as all tests have
    # completed.  It's not possible to do this check as each test finishes due to the fact that
    # the tests are running in parallel.  We could revise this so there's a dfuse method on
    # posix_tests class itself if required.
    for fuse in server.fuse_procs:
        conf.wf.add_test_case('fuse leak in tests',
                              'Test leaked dfuse instance at {}'.format(fuse),
                              test_class='test',)

    out_wrapper = None
    err_wrapper = None

    return pto.fatal_errors

def run_tests(dfuse):
    """Run some tests"""

    # pylint: disable=consider-using-with
    path = dfuse.dir

    fname = join(path, 'test_file3')

    rc = subprocess.run(['dd', 'if=/dev/zero', 'bs=16k', 'count=64', # nosec
                         'of={}'.format(join(path, 'dd_file'))],
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
    ofd.truncate(1024*1024)
    assert_file_size(ofd, 1024*1024)
    ofd.truncate(0)
    ofd.seek(0)
    ofd.write('simple file contents\n')
    ofd.flush()
    assert_file_size(ofd, 21)
    print(os.fstat(ofd.fileno()))
    ofd.close()
    ret = il_cmd(dfuse, ['cat', fname], check_write=False)
    assert ret.returncode == 0
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


lp = None
lt = None

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

    global lp
    global lt

    lp = __import__('cart_logparse')
    lt = __import__('cart_logtest')

    lt.wf = conf.wf

# https://stackoverflow.com/questions/1094841/get-human-readable-version-of-file-size
def sizeof_fmt(num, suffix='B'):
    """Return size as a human readable string"""
    for unit in ['', 'Ki', 'Mi', 'Gi', 'Ti', 'Pi', 'Ei', 'Zi']:
        if abs(num) < 1024.0:
            return "%3.1f%s%s" % (num, unit, suffix)
        num /= 1024.0
    return "%.1f%s%s" % (num, 'Yi', suffix)

def log_timer(func):
    """Wrapper around the log_test function to measure how long it takes"""

    def log_timer_wrapper(*args, **kwargs):
        """Do the actual wrapping"""

        conf = args[0]
        conf.lt.start()
        rc = None
        try:
            rc = func(*args, **kwargs)
        finally:
            conf.lt.stop()
        return rc

    return log_timer_wrapper

@log_timer
def log_test(conf,
             filename,
             show_memleaks=True,
             quiet=False,
             skip_fi=False,
             leak_wf=None,
             check_read=False,
             check_write=False,
             check_fstat=False):
    """Run the log checker on filename, logging to stdout"""

    # Check if the log file has wrapped, if it has then log parsing checks do
    # not work correctly.
    if os.path.exists('{}.old'.format(filename)):
        raise Exception('Log file exceeded max size')
    fstat = os.stat(filename)
    if fstat.st_size == 0:
        os.unlink(filename)
        return None
    if not quiet:
        print('Running log_test on {} {}'.format(filename,
                                                 sizeof_fmt(fstat.st_size)))

    log_iter = lp.LogIter(filename)

    # LogIter will have opened the file and seek through it as required, so start a background
    # process to compress it in parallel with the log tracing.
    conf.compress_file(filename)

    lto = lt.LogTest(log_iter, quiet=quiet)

    lto.hide_fi_calls = skip_fi

    try:
        lto.check_log_file(abort_on_warning=True,
                           show_memleaks=show_memleaks,
                           leak_wf=leak_wf)
    except lt.LogCheckError:
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
        raise Exception('Max log size exceeded, {} > {}'.format(sizeof_fmt(fstat.st_size),
                                                                sizeof_fmt(conf.max_log_size)))

    return lto.fi_location

def set_server_fi(server):
    """Run the client code to set server params"""

    # pylint: disable=consider-using-with

    cmd_env = get_base_env()

    cmd_env['OFI_INTERFACE'] = server.network_interface
    cmd_env['CRT_PHY_ADDR_STR'] = server.network_provider
    vh = ValgrindHelper(server.conf)

    if server.conf.args.memcheck == 'no':
        vh.use_valgrind = False

    system_name = 'daos_server'

    exec_cmd = vh.get_cmd_prefix()

    agent_bin = join(server.conf['PREFIX'], 'bin', 'daos_agent')

    addr_dir = tempfile.TemporaryDirectory(prefix='dnt_addr_',)
    addr_file = join(addr_dir.name, '{}.attach_info_tmp'.format(system_name))

    agent_cmd = [agent_bin,
                 '-i',
                 '-s',
                 server.agent_dir,
                 'dump-attachinfo',
                 '-o',
                 addr_file]

    rc = subprocess.run(agent_cmd, env=cmd_env, check=True)
    print(rc)

    cmd = ['set_fi_attr',
           '--cfg_path',
           addr_dir.name,
           '--group-name',
           'daos_server',
           '--rank',
           '0',
           '--attr',
           '0,0,0,0,0']

    exec_cmd.append(join(server.conf['PREFIX'], 'bin', 'cart_ctl'))
    exec_cmd.extend(cmd)

    prefix = 'dnt_crt_ctl_{}_'.format(get_inc_id())
    log_file = tempfile.NamedTemporaryFile(prefix=prefix,
                                           suffix='.log',
                                           delete=False)

    cmd_env['D_LOG_FILE'] = log_file.name
    cmd_env['DAOS_AGENT_DRPC_DIR'] = server.agent_dir

    rc = subprocess.run(exec_cmd,
                        env=cmd_env,
                        stdout=subprocess.PIPE,
                        stderr=subprocess.PIPE,
                        check=False)
    print(rc)
    vh.convert_xml()
    log_test(server.conf, log_file.name)
    assert rc.returncode == 0
    return False # fatal_errors

def create_and_read_via_il(dfuse, path):
    """Create file in dir, write to and read
    through the interception library"""

    fname = join(path, 'test_file')
    with open(fname, 'w') as ofd:
        ofd.write('hello ')
        ofd.write('world\n')
        ofd.flush()
        assert_file_size(ofd, 12)
        print(os.fstat(ofd.fileno()))
    ret = il_cmd(dfuse, ['cat', fname], check_write=False)
    assert ret.returncode == 0

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

    pool = server.get_test_pool()

    parent_dir = tempfile.TemporaryDirectory(dir=conf.dfuse_parent_dir,
                                             prefix='dnt_uns_')

    uns_dir = join(parent_dir.name, 'uns_ep')

    create_cont(conf, pool=pool, path=uns_dir)

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

    pool = server.get_test_pool()

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

    pool_stat = os.stat(join(dfuse.dir, pool))
    print('stat for {}'.format(pool))
    print(pool_stat)
    container = create_cont(server.conf, pool, ctype="POSIX")
    cdir = join(dfuse.dir, pool, container)
    fatal_errors.add_result(dfuse.stop())

    dfuse = DFuse(server, conf, pool=pool, caching=False)
    pre_stat = os.stat(dfuse.dir)
    dfuse.start(v_hint='pool_only')
    print('Running dfuse with pool only')
    stat_and_check(dfuse, pre_stat)
    check_no_file(dfuse)
    container2 = create_cont(server.conf, pool, ctype="POSIX")
    cpath = join(dfuse.dir, container2)
    print(os.listdir(cpath))
    cdir = join(dfuse.dir, container)
    create_and_read_via_il(dfuse, cdir)

    fatal_errors.add_result(dfuse.stop())

    dfuse = DFuse(server, conf, pool=pool, container=container, caching=False)
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


def run_in_fg(server, conf):
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
        container = create_cont(conf, pool.uuid, label=label, ctype="POSIX")

        # Only set the container cache attributes when the container is initially created so they
        # can be modified later.
        cont_attrs = OrderedDict()
        cont_attrs['dfuse-data-cache'] = False
        cont_attrs['dfuse-attr-time'] = 60
        cont_attrs['dfuse-dentry-time'] = 60
        cont_attrs['dfuse-ndentry-time'] = 60
        cont_attrs['dfuse-direct-io-disable'] = False

        for key, value in cont_attrs.items():
            run_daos_cmd(conf, ['container', 'set-attr', pool.label, container,
                                '--attr', key, '--value', str(value)],
                         show_stdout=True)

    dfuse = DFuse(server, conf, pool=pool.uuid, caching=True, wbcache=False)
    dfuse.log_flush = True
    dfuse.start()

    t_dir = join(dfuse.dir, container)

    print('Running at {}'.format(t_dir))
    print('export PATH={}:$PATH'.format(os.path.join(conf['PREFIX'], 'bin')))
    print('export LD_PRELOAD={}'.format(os.path.join(conf['PREFIX'], 'lib64', 'libioil.so')))
    print('export DAOS_AGENT_DRPC_DIR={}'.format(conf.agent_dir))
    print('export D_IL_REPORT=-1')
    print('daos container create --type POSIX --path {}/uns-link'.format(t_dir))
    print('daos container destroy --path {}/uns-link'.format(t_dir))
    print('daos cont list {}'.format(pool.label))
    try:
        dfuse.wait_for_exit()
    except KeyboardInterrupt:
        pass
    dfuse = None

def check_readdir_perf(server, conf):
    """ Check and report on readdir performance

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
        print('Populating to {}'.format(count))
        dir_dir = join(parent, 'dirs.{}.in'.format(count))
        t_dir = join(parent, 'dirs.{}'.format(count))
        file_dir = join(parent, 'files.{}.in'.format(count))
        t_file = join(parent, 'files.{}'.format(count))

        start_all = time.time()
        if not os.path.exists(t_dir):
            try:
                os.mkdir(dir_dir)
            except FileExistsError:
                pass
            for i in range(count):
                try:
                    os.mkdir(join(dir_dir, str(i)))
                except FileExistsError:
                    pass
            dir_time = time.time() - start_all
            print('Creating {} dirs took {:.2f}'.format(count, dir_time))
            os.rename(dir_dir, t_dir)

        if not os.path.exists(t_file):
            try:
                os.mkdir(file_dir)
            except FileExistsError:
                pass
            start = time.time()
            for i in range(count):
                with open(join(file_dir, str(i)), 'w'):
                    pass
            file_time = time.time() - start
            print('Creating {} files took {:.2f}'.format(count, file_time))
            os.rename(file_dir, t_file)

        return [dir_time, file_time]

    def print_results():
        """Display the results"""

        print(tabulate.tabulate(results,
                                headers=headers,
                                floatfmt=".2f"))

    pool = server.get_test_pool()

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

    all_start = time.time()

    while True:

        row = [count]
        row.extend(create_times)
        dfuse = DFuse(server, conf, pool=pool, container=container,
                      caching=False)
        dir_dir = join(dfuse.dir, 'dirs.{}'.format(count))
        file_dir = join(dfuse.dir, 'files.{}'.format(count))
        dfuse.start()
        start = time.time()
        subprocess.run(['/bin/ls', dir_dir], stdout=subprocess.PIPE, check=True)
        elapsed = time.time() - start
        print('processed {} dirs in {:.2f} seconds'.format(count,
                                                           elapsed))
        row.append(elapsed)
        dfuse.stop()
        dfuse = DFuse(server, conf, pool=pool, container=container,
                      caching=False)
        dfuse.start()
        start = time.time()
        subprocess.run(['/bin/ls', file_dir], stdout=subprocess.PIPE,
                       check=True)
        elapsed = time.time() - start
        print('processed {} files in {:.2f} seconds'.format(count,
                                                            elapsed))
        row.append(elapsed)
        dfuse.stop()

        dfuse = DFuse(server, conf, pool=pool, container=container,
                      caching=False)
        dfuse.start()
        start = time.time()
        subprocess.run(['/bin/ls', '-t', dir_dir], stdout=subprocess.PIPE,
                       check=True)
        elapsed = time.time() - start
        print('processed {} dirs in {:.2f} seconds'.format(count,
                                                           elapsed))
        row.append(elapsed)
        dfuse.stop()
        dfuse = DFuse(server, conf, pool=pool, container=container,
                      caching=False)
        dfuse.start()
        start = time.time()
        # Use sort by time here so ls calls stat, if you run ls -l then it will
        # also call getxattr twice which skews the figures.
        subprocess.run(['/bin/ls', '-t', file_dir], stdout=subprocess.PIPE,
                       check=True)
        elapsed = time.time() - start
        print('processed {} files in {:.2f} seconds'.format(count,
                                                            elapsed))
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
        start = time.time()
        subprocess.run(['/bin/ls', '-t', file_dir], stdout=subprocess.PIPE,
                       check=True)
        elapsed = time.time() - start
        print('processed {} files in {:.2f} seconds'.format(count,
                                                            elapsed))
        row.append(elapsed)
        start = time.time()
        subprocess.run(['/bin/ls', '-t', file_dir], stdout=subprocess.PIPE,
                       check=True)
        elapsed = time.time() - start
        print('processed {} files in {:.2f} seconds'.format(count,
                                                            elapsed))
        row.append(elapsed)
        results.append(row)

        elapsed = time.time() - all_start
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

    pool = server.get_test_pool()

    c_uuid = create_cont(conf, pool, ctype="PYTHON")

    container = daos.DCont(pool, c_uuid)

    kv = container.dict('my_test_kv')
    kv['a'] = 'a'
    kv['b'] = 'b'
    kv['list'] = pickle.dumps(list(range(1, 100000)))
    for k in range(1, 100):
        kv[str(k)] = pickle.dumps(list(range(1, 10)))
    print(type(kv))
    print(kv)
    print(kv['a'])

    print("First iteration")
    data = OrderedDict()
    for key in kv:
        print('key is {}, len {}'.format(key, len(kv[key])))
        print(type(kv[key]))
        data[key] = None

    print("Bulk loading")

    data['no-key'] = None

    kv.value_size = 32
    kv.bget(data, value_size=16)
    print("Default get value size %d", kv.value_size)
    print("Second iteration")
    failed = False
    for key in data:
        if data[key]:
            print('key is {}, len {}'.format(key, len(data[key])))
        elif key == 'no-key':
            pass
        else:
            failed = True
            print('Key is None {}'.format(key))

    if failed:
        print("That's not good")

    kv = None
    print('Closing container and opening new one')
    kv = container.get('my_test_kv')
    kv = None
    container = None
    # pylint: disable=protected-access
    daos._cleanup()
    log_test(conf, pydaos_log_file.name)

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

    def __init__(self, aft, cmd, env, loc):

        # pylint: disable=consider-using-with

        # The subprocess handle
        self._sp = None
        # The valgrind handle
        self.vh = None
        # The return from subprocess.poll
        self.ret = None

        self.cmd = cmd
        self.env = env
        self.aft = aft
        self._fi_file = None
        self.returncode = None
        self.stdout = None
        self.stderr = None
        self.fi_loc = None
        self.fault_injected = None
        self.loc = loc

        if loc is None:
            prefix = 'dnt_fi_{}_reference_'.format(aft.description)
        else:
            prefix = 'dnt_fi_{}_{:04d}_'.format(aft.description, loc)
        self.log_file = tempfile.NamedTemporaryFile(prefix=prefix,
                                                    suffix='.log',
                                                    dir=self.aft.conf.tmp_dir,
                                                    delete=False).name
        self.env['D_LOG_FILE'] = self.log_file

    def __str__(self):
        res = "Fault injection test of '{}'\n".format(' '.join(self.cmd))
        res += 'Fault injection location {}\n'.format(self.loc)
        if self.vh:
            res += 'Valgrind enabled for this test\n'
        if self.returncode is None:
            res += 'Process not completed'
        else:
            res += 'Returncode was {}'.format(self.returncode)

        if self.stdout:
            res += '\nSTDOUT:{}'.format(self.stdout.decode('utf-8').strip())

        if self.stderr:
            res += '\nSTDERR:{}'.format(self.stderr.decode('utf-8').strip())
        return res

    def start(self):
        """Start the command"""
        fc = {}

        fc['fault_config'] = [{'id': 100,
                               'probability_x': 1,
                               'probability_y': 1}]

        if self.loc:
            fc['fault_config'].append({'id': 0,
                                       'probability_x': 1,
                                       'probability_y': 1,
                                       'interval': self.loc,
                                       'max_faults': 1})

            if self.aft.skip_daos_init:
                fc['fault_config'].append({'id': 101, 'probability_x': 1})

        # pylint: disable=consider-using-with
        self._fi_file = tempfile.NamedTemporaryFile(prefix='fi_', suffix='.yaml')

        self._fi_file.write(yaml.dump(fc, encoding='utf=8'))
        self._fi_file.flush()

        self.env['D_FI_CONFIG'] = self._fi_file.name

        if self.vh:
            exec_cmd = self.vh.get_cmd_prefix()
            exec_cmd.extend(self.cmd)
        else:
            exec_cmd = self.cmd

        self._sp = subprocess.Popen(exec_cmd,
                                    env=self.env,
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
            self.aft.wf.explain(self.fi_loc, os.path.basename(self.log_file), fi_signal)
            self.aft.conf.wf.explain(self.fi_loc, os.path.basename(self.log_file), fi_signal)
        # Put in a new-line.
        print()
        self.returncode = rc
        self.stdout = self._sp.stdout.read()
        self.stderr = self._sp.stderr.read()

        show_memleaks = True

        fi_signal = None
        # A negative return code means the process exited with a signal so do
        # not check for memory leaks in this case as it adds noise, right when
        # it's least wanted.
        if rc < 0:
            show_memleaks = False
            fi_signal = -rc

        try:
            if self.loc:
                wf = self.aft.wf
            else:
                wf = None
            self.fi_loc = log_test(self.aft.conf,
                                   self.log_file,
                                   show_memleaks=show_memleaks,
                                   quiet=True,
                                   skip_fi=True,
                                   leak_wf=wf)
            self.fault_injected = True
            assert self.fi_loc
        except NLTestNoFi:
            # If a fault wasn't injected then check output is as expected.
            # It's not possible to log these as warnings, because there is
            # no src line to log them against, so simply assert.
            assert self.returncode == 0, self

            if self.aft.check_post_stdout:
                assert self.stderr == b''
                if self.aft.expected_stdout is not None:
                    assert self.stdout == self.aft.expected_stdout
            self.fault_injected = False
        if self.vh:
            self.vh.convert_xml()
        if not self.fault_injected:
            _explain()
            return
        if not self.aft.check_stderr:
            _explain()
            return

        # Check stderr from a daos command.
        # These should mostly be from the DH_PERROR_SYS or DH_PERROR_DER macros so check for
        # this format.  There may be multiple lines and the two styles may be mixed.
        # These checks will report an error against the line of code that introduced the "leak"
        # which may well only have a loose correlation to where the error was reported.
        if self.aft.check_daos_stderr:
            stderr = self.stderr.decode('utf-8').rstrip()
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

                if 'DER_UNKNOWN' in line:
                    self.aft.wf.add(self.fi_loc,
                                    'HIGH',
                                    "Incorrect stderr '{}'".format(line),
                                    mtype='Invalid error code used')
                    continue

                self.aft.wf.add(self.fi_loc,
                                'NORMAL',
                                "Unexpected stderr '{}'".format(line),
                                mtype='Unrecognised error')
            _explain()
            return

        if self.returncode == 0:
            if self.stdout != self.aft.expected_stdout:
                self.aft.wf.add(self.fi_loc,
                                'NORMAL',
                                "Incorrect stdout '{}'".format(self.stdout),
                                mtype='Out of memory caused zero exit '
                                'code with incorrect output')

        stderr = self.stderr.decode('utf-8').rstrip()
        if not stderr.endswith("(-1009): Out of memory") and \
           'error parsing command line arguments' not in stderr and \
           self.stdout != self.aft.expected_stdout:
            if self.stdout != b'':
                print(self.aft.expected_stdout)
                print()
                print(self.stdout)
                print()
            self.aft.wf.add(self.fi_loc,
                            'NORMAL',
                            "Incorrect stderr '{}'".format(stderr),
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
        # Check stderr from commands where faults were injected.
        self.check_stderr = True
        # Check stdout/error from commands where faults were not injected
        self.check_post_stdout = True
        # Check stderr conforms to daos_hdlr.c style
        self.check_daos_stderr = False
        self.expected_stdout = None
        self.use_il = False
        self.wf = conf.wf
        # Instruct the fault injection code to skip daos_init().
        self.skip_daos_init = True

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

        num_cores = len(os.sched_getaffinity(0))

        if num_cores < 20:
            max_child = 1
        else:
            max_child = int(num_cores / 4 * 3)

        print('Maximum number of spawned tests will be {}'.format(max_child))

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
                print(ret)
                if ret.returncode < 0:
                    fatal_errors = True
                    to_rerun.append(ret.loc)

                if not ret.fault_injected:
                    print('Fault injection did not trigger, stopping')
                    finished = True
                break

        print('Completed, fid {}'.format(fid))
        print('Max in flight {}'.format(max_count))

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

        cmd_env['DAOS_AGENT_DRPC_DIR'] = self.conf.agent_dir

        if callable(self.cmd):
            cmd = self.cmd()
        else:
            cmd = self.cmd

        aftf = AllocFailTestRun(self, cmd, cmd_env, loc)
        if valgrind:
            aftf.vh = ValgrindHelper(self.conf)
            # Turn off leak checking in this case, as we're just interested in
            # why it crashed.
            aftf.vh.full_check = False

        aftf.start()

        return aftf

def test_dfuse_start(server, conf, wf):
    """Start dfuse under fault injection

    This test will check error paths for faults that can occur whilst starting
    dfuse.  To do this it injects a fault into dfuse just before dfuse_session_mount
    so that it always returns immediately rather than registering with the kernel
    and then it runs dfuse up to this point checking the error paths.
    """
    pool = server.get_test_pool()

    container = create_cont(conf, pool, ctype='POSIX')

    mount_point = join(conf.dfuse_parent_dir, 'fi-mount')

    os.mkdir(mount_point)

    cmd = [join(conf['PREFIX'], 'bin', 'dfuse'),
           '--mountpoint', mount_point,
           '--pool', pool, '--cont', container, '--foreground', '--singlethread']

    test_cmd = AllocFailTest(conf, 'dfuse', cmd)
    test_cmd.wf = wf
    test_cmd.check_daos_stderr = True
    test_cmd.check_post_stdout = False
    test_cmd.check_stderr = True

    rc = test_cmd.launch()
    os.rmdir(mount_point)
    return rc

def test_alloc_fail_copy(server, conf, wf):
    """Run container (filesystem) copy under fault injection.

    This test will create a new uuid per iteration, and the test will then try to create a matching
    container so this is potentially resource intensive.

    There are lots of errors in the stdout/stderr of this command which we need to work through but
    are not yet checked for.
    """

    # pylint: disable=consider-using-with

    pool = server.get_test_pool()
    src_dir = tempfile.TemporaryDirectory(prefix='copy_src_',)
    sub_dir = join(src_dir.name, 'new_dir')
    os.mkdir(sub_dir)
    for f in range(5):
        with open(join(sub_dir, 'file.{}'.format(f)), 'w') as ofd:
            ofd.write('hello')

    os.symlink('broken', join(sub_dir, 'broken_s'))
    os.symlink('file.0', join(sub_dir, 'link'))

    def get_cmd():
        container = str(uuid.uuid4())
        cmd = [join(conf['PREFIX'], 'bin', 'daos'),
               'filesystem',
               'copy',
               '--src',
               src_dir.name,
               '--dst',
               'daos://{}/{}'.format(pool, container)]
        return cmd

    test_cmd = AllocFailTest(conf, 'filesystem-copy', get_cmd)
    test_cmd.skip_daos_init = False
    test_cmd.wf = wf
    test_cmd.check_daos_stderr = True
    test_cmd.check_post_stdout = False
    test_cmd.check_stderr = True

    rc = test_cmd.launch()
    return rc

def test_alloc_fail_cat(server, conf):
    """Run the Interception library with fault injection

    Start dfuse for this test, and do not do output checking on the command
    itself yet.
    """

    pool = server.get_test_pool()
    container = create_cont(conf, pool, ctype='POSIX', label='fault_inject')

    dfuse = DFuse(server, conf, pool=pool, container=container)
    dfuse.use_valgrind = False
    dfuse.start()

    target_file = join(dfuse.dir, 'test_file')

    with open(target_file, 'w') as fd:
        fd.write('Hello there')

    test_cmd = AllocFailTest(conf, 'il-cat', ['cat', target_file])
    test_cmd.use_il = True
    test_cmd.check_stderr = False
    test_cmd.wf = conf.wf

    rc = test_cmd.launch()
    dfuse.stop()
    return rc


def test_fi_list_attr(server, conf, wf):
    """Run daos cont list-attr with fault injection"""

    pool = server.get_test_pool()

    container = create_cont(conf, pool)

    run_daos_cmd(conf,
                 ['container', 'set-attr',
                  pool, container,
                  '--attr', 'my-test-attr-1', '--value', 'some-value'])

    run_daos_cmd(conf,
                 ['container', 'set-attr',
                  pool, container,
                  '--attr', 'my-test-attr-2', '--value', 'some-other-value'])

    cmd = [join(conf['PREFIX'], 'bin', 'daos'),
           'container',
           'list-attrs',
           pool,
           container]

    test_cmd = AllocFailTest(conf, 'cont-list-attr', cmd)
    test_cmd.wf = wf

    rc = test_cmd.launch()
    destroy_container(conf, pool, container)
    return rc


def test_fi_get_attr(server, conf, wf):
    """Run daos cont get-attr with fault injection"""

    pool = server.get_test_pool_id()

    container = create_cont(conf, pool)

    attr_name = 'my-test-attr'

    run_daos_cmd(conf,
                 ['container', 'set-attr',
                  pool, container,
                  '--attr', attr_name, '--value', 'value'])

    cmd = [join(conf['PREFIX'], 'bin', 'daos'),
           'container',
           'get-attr',
           pool,
           container,
           attr_name]

    test_cmd = AllocFailTest(conf, 'cont-get-attr', cmd)
    test_cmd.wf = wf

    test_cmd.check_daos_stderr = True
    test_cmd.check_post_stdout = False
    test_cmd.check_stderr = True

    rc = test_cmd.launch()
    destroy_container(conf, pool, container)
    return rc


def test_fi_cont_query(server, conf, wf):
    """Run daos cont query with fault injection"""

    pool = server.get_test_pool_id()

    container = create_cont(conf, pool, ctype='POSIX')

    cmd = [join(conf['PREFIX'], 'bin', 'daos'),
           'container',
           'query',
           pool,
           container]

    test_cmd = AllocFailTest(conf, 'cont-query', cmd)
    test_cmd.wf = wf

    test_cmd.check_daos_stderr = True
    test_cmd.check_post_stdout = False
    test_cmd.check_stderr = True

    rc = test_cmd.launch()
    destroy_container(conf, pool, container)
    return rc


def test_fi_cont_check(server, conf, wf):
    """Run daos cont check with fault injection"""

    pool = server.get_test_pool_id()

    container = create_cont(conf, pool)

    cmd = [join(conf['PREFIX'], 'bin', 'daos'),
           'container',
           'check',
           pool,
           container]

    test_cmd = AllocFailTest(conf, 'cont-check', cmd)
    test_cmd.wf = wf

    test_cmd.check_daos_stderr = True
    test_cmd.check_post_stdout = False
    test_cmd.check_stderr = True

    rc = test_cmd.launch()
    destroy_container(conf, pool, container)
    return rc


def test_alloc_fail(server, conf):
    """run 'daos' client binary with fault injection"""

    pool = server.get_test_pool()

    cmd = [join(conf['PREFIX'], 'bin', 'daos'),
           'cont',
           'list',
           pool]
    test_cmd = AllocFailTest(conf, 'pool-list-containers', cmd)

    # Create at least one container, and record what the output should be when
    # the command works.
    container = create_cont(conf, pool)

    rc = test_cmd.launch()
    destroy_container(conf, pool, container)
    return rc


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
        with DaosServer(conf, test_class='first', wf=wf_server, fe=fatal_errors) as server:
            if args.mode == 'launch':
                run_in_fg(server, conf)
            elif args.mode == 'kv':
                test_pydaos_kv(server, conf)
            elif args.mode == 'overlay':
                fatal_errors.add_result(run_duns_overlay_test(server, conf))
            elif args.mode == 'set-fi':
                fatal_errors.add_result(set_server_fi(server))
            elif args.mode == 'all':
                fi_test_dfuse = True
                fatal_errors.add_result(run_posix_tests(server, conf))
                fatal_errors.add_result(run_dfuse(server, conf))
                fatal_errors.add_result(run_duns_overlay_test(server, conf))
                test_pydaos_kv(server, conf)
                fatal_errors.add_result(set_server_fi(server))
            elif args.test == 'all':
                fatal_errors.add_result(run_posix_tests(server, conf))
            elif args.test:
                fatal_errors.add_result(run_posix_tests(server, conf, args.test))
            else:
                fatal_errors.add_result(run_posix_tests(server, conf))
                fatal_errors.add_result(run_dfuse(server, conf))
                fatal_errors.add_result(set_server_fi(server))

    if args.mode == 'all':
        with DaosServer(conf, wf=wf_server, fe=fatal_errors) as server:
            pass

    # If running all tests then restart the server under valgrind.
    # This is really, really slow so just do cont list, then
    # exit again.
    if args.mode == 'server-valgrind':
        with DaosServer(conf, valgrind=True, test_class='valgrind',
                        wf=wf_server, fe=fatal_errors) as server:
            pools = server.fetch_pools()
            for pool in pools:
                cmd = ['cont', 'list', pool.id()]
                run_daos_cmd(conf, cmd, valgrind=False)

    # If the perf-check option is given then re-start everything without much
    # debugging enabled and run some micro-benchmarks to give numbers for use
    # as a comparison against other builds.
    if args.perf_check or fi_test or fi_test_dfuse:
        args.server_debug = 'INFO'
        args.memcheck = 'no'
        args.dfuse_debug = 'WARN'
        with DaosServer(conf, test_class='no-debug', wf=wf_server, fe=fatal_errors) as server:
            if fi_test:
                # Most of the fault injection tests go here, they are then run on docker containers
                # so can be performed in parallel.

                wf_client = WarningsFactory('nlt-client-leaks.json')

                # dfuse startup, uses custom fault to force exit if no other faults injected.
                fatal_errors.add_result(test_dfuse_start(server, conf, wf_client))

                # list-container test.
                fatal_errors.add_result(test_alloc_fail(server, conf))

                # Container query test.
                fatal_errors.add_result(test_fi_cont_query(server, conf, wf_client))

                fatal_errors.add_result(test_fi_cont_check(server, conf, wf_client))

                # Container attribute tests
                fatal_errors.add_result(test_fi_get_attr(server, conf, wf_client))
                fatal_errors.add_result(test_fi_list_attr(server, conf, wf_client))

                # filesystem copy test.
                fatal_errors.add_result(test_alloc_fail_copy(server, conf, wf_client))

                wf_client.close()

            if fi_test_dfuse:
                # We cannot yet run dfuse inside docker containers and some of the failure modes
                # aren't well handled so continue to run the dfuse fault injection test on real
                # hardware.

                # Read-via-IL test, requires dfuse.
                fatal_errors.add_result(test_alloc_fail_cat(server, conf))

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
    print('Total time in log analysis: {:.2f} seconds'.format(conf.lt.total))
    print('Total time in log compression: {:.2f} seconds'.format(conf.lt_compress.total))
    return fatal_errors

def main():
    """Wrap the core function, and catch/report any exceptions

    This allows the junit results to show at least a stack trace and assertion message for
    any failure, regardless of if it's from a test case or not.
    """

    parser = argparse.ArgumentParser(description='Run DAOS client on local node')
    parser.add_argument('--server-debug', default=None)
    parser.add_argument('--dfuse-debug', default=None)
    parser.add_argument('--class-name', default=None, help='class name to use for junit')
    parser.add_argument('--memcheck', default='some', choices=['yes', 'no', 'some'])
    parser.add_argument('--no-root', action='store_true')
    parser.add_argument('--max-log-size', default=None)
    parser.add_argument('--engine-count', type=int, default=1, help='Number of daos engines to run')
    parser.add_argument('--dfuse-dir', default='/tmp', help='parent directory for all dfuse mounts')
    parser.add_argument('--perf-check', action='store_true')
    parser.add_argument('--dtx', action='store_true')
    parser.add_argument('--test', help="Use '--test list' for list")
    parser.add_argument('mode', nargs='?')
    args = parser.parse_args()

    if args.mode and args.test:
        print('Cannot use mode and test')
        sys.exit(1)

    if args.test == 'list':
        tests = []
        for fn in dir(posix_tests):
            if fn.startswith('test'):
                tests.append(fn[5:])
        print('Tests are: {}'.format(','.join(sorted(tests))))
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
