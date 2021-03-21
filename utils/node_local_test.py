#!/usr/bin/python3
"""
Node local test (NLT).

Test script for running DAOS on a single node over tmpfs and running initial
smoke/unit tests.

Includes support for DFuse with a number of unit tests, as well as stressing
the client with fault injection of D_ALLOC() usage.
"""

# pylint: disable=too-many-lines
# pylint: disable=too-few-public-methods
# pylint: disable=protected-access

import os
import bz2
import sys
import time
import uuid
import json
import signal
import stat
import errno
import argparse
import tabulate
import functools
import subprocess
import tempfile
import pickle
import xattr
from collections import OrderedDict
import yaml

class NLTestFail(Exception):
    """Used to indicate test failure"""
    pass

class NLTestNoFi(NLTestFail):
    """Used to indicate Fault injection didn't work"""
    pass

class NLTestNoFunction(NLTestFail):
    """Used to indicate a function did not log anything"""

    def __init__(self, function):
        super().__init__(self)
        self.function = function

class NLTestTimeout(NLTestFail):
    """Used to indicate that an operation timed out"""
    pass

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
    ret = subprocess.run(cmd)
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

        self.dfuse_parent_dir = tempfile.mkdtemp(dir=args.dfuse_dir,
                                                 prefix='dnt_dfuse_')

    def __del__(self):
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

    def __init__(self, filename):
        self._fd = open(filename, 'w')
        self.filename = filename
        self.issues = []
        self.pending = []
        self._running = True
        # Save the filename of the object, as __file__ does not
        # work in __del__
        self._file = __file__.lstrip('./')
        self._flush()

    def __del__(self):
        """Ensure the file is flushed on exit, but if it hasn't already
        been closed then mark an error"""
        if not self._fd:
            return

        entry = {}
        entry['fileName'] = os.path.basename(self._file)
        entry['directory'] = os.path.dirname(self._file)
        # pylint: disable=protected-access
        entry['lineStart'] = sys._getframe().f_lineno
        entry['message'] = 'Tests exited without shutting down properly'
        entry['severity'] = 'ERROR'
        self.issues.append(entry)
        self.close()

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
            print('Nothing to explain')
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
        entry['directory'] = os.path.dirname(line.filename)
        entry['fileName'] = os.path.basename(line.filename)
        if mtype:
            entry['type'] = mtype
        else:
            entry['type'] = message
        if cat:
            entry['category'] = cat
        entry['lineStart'] = line.lineno
        entry['description'] = message
        entry['message'] = line.get_anon_msg()
        entry['severity'] = sev
        self.issues.append(entry)
        if self.pending and self.pending[0][0].pid != line.pid:
            self.reset_pending()
        self.pending.append((line, message))
        self._flush()

    def reset_pending(self):
        """Reset the pending list

        Should be called before iterating on each new file, so errors
        from previous files aren't attribured to new files.
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
            entry['fileName'] = os.path.basename(self._file)
            entry['directory'] = os.path.dirname(self._file)
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

def load_conf(args):
    """Load the build config file"""
    file_self = os.path.dirname(os.path.abspath(__file__))
    json_file = None
    while True:
        new_file = os.path.join(file_self, '.build_vars.json')
        if os.path.exists(new_file):
            json_file = new_file
            break
        file_self = os.path.dirname(file_self)
        if file_self == '/':
            raise Exception('build file not found')
    ofh = open(json_file, 'r')
    conf = json.load(ofh)
    ofh.close()
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

class DaosServer():
    """Manage a DAOS server instance"""

    def __init__(self, conf, valgrind=False):
        self.running = False
        self._file = __file__.lstrip('./')
        self._sp = None
        self.conf = conf
        self.valgrind = valgrind
        self._agent = None
        self.control_log = tempfile.NamedTemporaryFile(prefix='dnt_control_',
                                                       suffix='.log',
                                                       delete=False)
        self.agent_log = tempfile.NamedTemporaryFile(prefix='dnt_agent_',
                                                     suffix='.log',
                                                     delete=False)
        self.server_log = tempfile.NamedTemporaryFile(prefix='dnt_server_',
                                                      suffix='.log',
                                                      delete=False)
        self.__process_name = 'daos_engine'
        if self.valgrind:
            self.__process_name = 'valgrind'

        socket_dir = '/tmp/dnt_sockets'
        if not os.path.exists(socket_dir):
            os.mkdir(socket_dir)

        self.agent_dir = tempfile.mkdtemp(prefix='dnt_agent_')

        self._yaml_file = None
        self._io_server_dir = None
        self._size = os.statvfs('/mnt/daos')
        capacity = self._size.f_blocks * self._size.f_bsize
        mb = int(capacity / (1024*1024))
        self.mb = mb

    def __del__(self):
        if self.running:
            self.stop(None)
        os.rmdir(self.agent_dir)

    # pylint: disable=no-self-use
    def _check_timing(self, op, start, max_time):
        elapsed = time.time() - start
        if elapsed > max_time:
            raise NLTestTimeout("{} failed after {:.2f}s (max {:.2f}s)".format(
                op, elapsed, max_time))

    def _check_system_state(self, desired_states):
        if not isinstance(desired_states, list):
            desired_states = [desired_states]

        rc = self.run_dmg(['system', 'query', '--json'])
        if rc.returncode == 0:
            data = json.loads(rc.stdout.decode('utf-8'))
            members = data['response']['members']
            if members is not None:
                for desired_state in desired_states:
                    if members[0]['state'] == desired_state:
                        return True
        return False

    def start(self):
        """Start a DAOS server"""

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

            fd = open(os.path.join(self._io_server_dir.name,
                                   'daos_engine'), 'w')
            fd.write('#!/bin/sh\n')
            fd.write('export PATH=$REAL_PATH\n')
            fd.write('exec valgrind {} daos_engine "$@"\n'.format(
                ' '.join(valgrind_args)))
            fd.close()

            os.chmod(os.path.join(self._io_server_dir.name, 'daos_engine'),
                     stat.S_IXUSR | stat.S_IRUSR)

            server_env['REAL_PATH'] = '{}:{}'.format(
                os.path.join(self.conf['PREFIX'], 'bin'), server_env['PATH'])
            server_env['PATH'] = '{}:{}'.format(self._io_server_dir.name,
                                                server_env['PATH'])

        daos_server = os.path.join(self.conf['PREFIX'], 'bin', 'daos_server')

        self_dir = os.path.dirname(os.path.abspath(__file__))

        # Create a server yaml file.  To do this open and copy the
        # nlt_server.yaml file in the current directory, but overwrite
        # the server log file with a temporary file so that multiple
        # server runs do not overwrite each other.
        scfd = open(os.path.join(self_dir, 'nlt_server.yaml'), 'r')

        scyaml = yaml.safe_load(scfd)
        scyaml['engines'][0]['log_file'] = self.server_log.name
        if self.conf.args.server_debug:
            scyaml['control_log_mask'] = 'ERROR'
            scyaml['engines'][0]['log_mask'] = self.conf.args.server_debug
        scyaml['control_log_file'] = self.control_log.name

        for (key, value) in server_env.items():
            scyaml['engines'][0]['env_vars'].append('{}={}'.format(key, value))

        self._yaml_file = tempfile.NamedTemporaryFile(
            prefix='nlt-server-config-',
            suffix='.yaml')

        self._yaml_file.write(yaml.dump(scyaml, encoding='utf-8'))
        self._yaml_file.flush()

        cmd = [daos_server, '--config={}'.format(self._yaml_file.name),
               'start', '-t' '4', '--insecure', '-d', self.agent_dir]

        self._sp = subprocess.Popen(cmd)

        agent_config = os.path.join(self_dir, 'nlt_agent.yaml')

        agent_bin = os.path.join(self.conf['PREFIX'], 'bin', 'daos_agent')

        agent_cmd = [agent_bin,
                     '--config-path', agent_config,
                     '--insecure',
                     '--runtime_dir', self.agent_dir,
                     '--logfile', self.agent_log.name]

        if not self.conf.args.server_debug:
            agent_cmd.append('--debug')

        self._agent = subprocess.Popen(agent_cmd)
        self.conf.agent_dir = self.agent_dir
        self.running = True

        # Configure the storage.  DAOS wants to mount /mnt/daos itself if not
        # already mounted, so let it do that.
        # This code supports three modes of operation:
        # /mnt/daos is not mounted.  It will be mounted and formatted.
        # /mnt/daos is mounted but empty.  It will be remounted and formatted
        # /mnt/daos exists and has data in.  It will be used as is.
        start = time.time()
        max_start_time = 30

        cmd = ['storage', 'format']
        while True:
            time.sleep(0.5)
            rc = self.run_dmg(cmd)
            ready = False
            if rc.returncode == 1:
                for line in rc.stdout.decode('utf-8').splitlines():
                    if 'format storage of running instance' in line:
                        ready = True
                    format_message = ('format request for already-formatted'
                                      ' storage and reformat not specified')
                    if format_message in line:
                        cmd = ['storage', 'format', '--reformat']
                for line in rc.stderr.decode('utf-8').splitlines():
                    if 'system reformat requires the following' in line:
                        ready = True
            if ready:
                break
            self._check_timing("format", start, max_start_time)
        print('Format completion in {:.2f} seconds'.format(time.time() - start))

        # How wait until the system is up, basically the format to happen.
        while True:
            time.sleep(0.5)
            if self._check_system_state(['ready', 'joined']):
                break
            self._check_timing("start", start, max_start_time)
        print('Server started in {:.2f} seconds'.format(time.time() - start))

    def stop(self, wf):
        """Stop a previously started DAOS server"""
        if self._agent:
            self._agent.send_signal(signal.SIGINT)
            ret = self._agent.wait(timeout=5)
            print('rc from agent is {}'.format(ret))

        if not self._sp:
            return

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
            fd = open(status_file, 'r')
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

        if len(procs) != 1:
            entry = {}
            entry['fileName'] = os.path.basename(self._file)
            entry['directory'] = os.path.dirname(self._file)
            # pylint: disable=protected-access
            entry['lineStart'] = sys._getframe().f_lineno
            entry['severity'] = 'ERROR'
            entry['message'] = 'daos_engine died during testing'
            self.conf.wf.issues.append(entry)

        rc = self.run_dmg(['system', 'stop'])
        assert rc.returncode == 0 # nosec

        start = time.time()
        max_stop_time = 5
        while True:
            time.sleep(0.5)
            if self._check_system_state('stopped'):
                break
            try:
                self._check_timing("stop", start, max_stop_time)
            except NLTestTimeout as e:
                print('Failed to stop: {}'.format(e))
                if time.time() - start > 30:
                    raise
        print('Server stopped in {:.2f} seconds'.format(time.time() - start))

        self._sp.send_signal(signal.SIGTERM)
        ret = self._sp.wait(timeout=5)
        print('rc from server is {}'.format(ret))

        compress_file(self.agent_log.name)
        compress_file(self.control_log.name)

        log_test(self.conf, self.server_log.name, leak_wf=wf)
        self.running = False
        return ret

    def run_dmg(self, cmd):
        """Run the specified dmg command"""

        exe_cmd = [os.path.join(self.conf['PREFIX'], 'bin', 'dmg')]
        exe_cmd.append('--insecure')
        exe_cmd.extend(cmd)

        print('running {}'.format(exe_cmd))
        return subprocess.run(exe_cmd,
                              stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE)

def il_cmd(dfuse, cmd, check_read=True, check_write=True):
    """Run a command under the interception library

    Do not run valgrind here, not because it's not useful
    but the options needed are different.  Valgrind handles
    linking differently so some memory is wrongly lost that
    would be freed in the _fini() function, and a lot of
    commands do not free all memory anyway.
    """
    my_env = get_base_env()
    prefix = 'dnt_dfuse_il_{}_'.format(get_inc_id())
    log_file = tempfile.NamedTemporaryFile(prefix=prefix,
                                           suffix='.log',
                                           delete=False)
    my_env['D_LOG_FILE'] = log_file.name
    my_env['LD_PRELOAD'] = os.path.join(dfuse.conf['PREFIX'],
                                        'lib64', 'libioil.so')
    my_env['DAOS_AGENT_DRPC_DIR'] = dfuse._daos.agent_dir
    ret = subprocess.run(cmd, env=my_env)
    print('Logged il to {}'.format(log_file.name))
    print(ret)

    try:
        log_test(dfuse.conf,
                 log_file.name,
                 check_read=check_read,
                 check_write=check_write)
        assert ret.returncode == 0 # nosec
    except NLTestNoFunction as error:
        print("ERROR: command '{}' did not log via {}".format(' '.join(cmd),
                                                              error.function))
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

        self._xml_file = 'dnt.{}.memcheck'.format(self._logid)

        cmd = ['valgrind', '--fair-sched=yes']

        if self.full_check:
            cmd.extend(['--leak-check=full', '--show-leak-kinds=all'])
        else:
            cmd.append('--leak-check=no')

        src_suppression_file = os.path.join('src',
                                            'cart',
                                            'utils',
                                            'memcheck-cart.supp')
        if os.path.exists(src_suppression_file):
            cmd.append('--suppressions={}'.format(src_suppression_file))
        else:
            cmd.append('--suppressions={}'.format(
                os.path.join(self.conf['PREFIX'],
                             'etc',
                             'memcheck-cart.supp')))

        cmd.append('--error-exitcode=42')

        cmd.extend(['--xml=yes',
                    '--xml-file={}'.format(self._xml_file)])
        return cmd

    def convert_xml(self):
        """Modify the xml file"""

        if not self.use_valgrind:
            return
        fd = open(self._xml_file, 'r')
        ofd = open('{}.xml'.format(self._xml_file), 'w')
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
                 path=None,
                 caching=False):
        if path:
            self.dir = path
        else:
            self.dir = os.path.join(conf.dfuse_parent_dir, 'dfuse_mount')
        self.pool = pool
        self.valgrind_file = None
        self.container = container
        self.conf = conf
        # Detect the number of cores and do something sensible, if there are
        # more than 32 on the node then use 12, otherwise use the whole node.
        num_cores = len(os.sched_getaffinity(0))
        if num_cores > 32:
            self.cores = 12
        else:
            self.cores = None
        self._daos = daos
        self.caching = caching
        self.use_valgrind = True
        self._sp = None

        self.log_file = None

        self.valgrind = None
        if not os.path.exists(self.dir):
            os.mkdir(self.dir)

    def start(self, v_hint=None):
        """Start a dfuse instance"""
        dfuse_bin = os.path.join(self.conf['PREFIX'], 'bin', 'dfuse')

        single_threaded = False

        pre_inode = os.stat(self.dir).st_ino

        my_env = get_base_env()

        if self.conf.args.dfuse_debug:
            my_env['D_LOG_MASK'] = self.conf.args.dfuse_debug

        if v_hint is None:
            v_hint = get_inc_id()

        prefix = 'dnt_dfuse_{}_'.format(v_hint)
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

        cmd.extend([dfuse_bin, '-m', self.dir, '-f'])

        if single_threaded:
            cmd.append('-S')

        if self.caching:
            cmd.append('--enable-caching')

        if self.pool:
            cmd.extend(['--pool', self.pool])
        if self.container:
            cmd.extend(['--container', self.container])
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
                raise Exception('dfuse died waiting for start')
            except subprocess.TimeoutExpired:
                pass
            total_time += 1
            if total_time > 60:
                raise Exception('Timeout starting dfuse')

    def _close_files(self):
        work_done = False
        for fname in os.listdir('/proc/self/fd'):
            try:
                tfile = os.readlink(os.path.join('/proc/self/fd', fname))
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
            if ret != 0:
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

def get_pool_list():
    """Return a list of valid pool names"""
    pools = []

    for fname in os.listdir('/mnt/daos'):
        if len(fname) != 36:
            continue
        try:
            uuid.UUID(fname)
        except ValueError:
            continue
        pools.append(fname)
    return pools

def assert_file_size_fd(fd, size):
    """Verify the file size is as expected"""
    my_stat = os.fstat(fd)
    print('Checking file size is {} {}'.format(size, my_stat.st_size))
    assert my_stat.st_size == size # nosec

def assert_file_size(ofd, size):
    """Verify the file size is as expected"""
    assert_file_size_fd(ofd.fileno(), size)

def import_daos(server, conf):
    """Return a handle to the pydaos module"""

    if sys.version_info.major < 3:
        pydir = 'python{}.{}'.format(sys.version_info.major,
                                     sys.version_info.minor)
    else:
        pydir = 'python{}'.format(sys.version_info.major)

    sys.path.append(os.path.join(conf['PREFIX'],
                                 'lib64',
                                 pydir,
                                 'site-packages'))

    os.environ['DD_MASK'] = 'all'
    os.environ['DD_SUBSYS'] = 'all'
    os.environ['D_LOG_MASK'] = 'DEBUG'
    os.environ['FI_UNIVERSE_SIZE'] = '128'
    os.environ['DAOS_AGENT_DRPC_DIR'] = server.agent_dir

    daos = __import__('pydaos')
    return daos

def run_daos_cmd(conf,
                 cmd,
                 valgrind=True):
    """Run a DAOS command

    Run a command, returning what subprocess.run() would.

    Enable logging, and valgrind for the command.

    if prefix is set to False do not run a DAOS command, but instead run what's
    provided, however run it under the IL.
    """
    vh = ValgrindHelper(conf)

    if conf.args.memcheck == 'no':
        valgrind = False

    if not valgrind:
        vh.use_valgrind = False

    exec_cmd = vh.get_cmd_prefix()
    exec_cmd.append(os.path.join(conf['PREFIX'], 'bin', 'daos'))
    exec_cmd.extend(cmd)

    cmd_env = get_base_env()

    prefix = 'dnt_cmd_{}_'.format(get_inc_id())
    log_file = tempfile.NamedTemporaryFile(prefix=prefix,
                                           suffix='.log',
                                           delete=False)

    cmd_env['D_LOG_FILE'] = log_file.name
    cmd_env['DAOS_AGENT_DRPC_DIR'] = conf.agent_dir

    rc = subprocess.run(exec_cmd,
                        stdout=subprocess.PIPE,
                        stderr=subprocess.PIPE,
                        env=cmd_env)

    if rc.stderr != b'':
        print('Stderr from command')
        print(rc.stderr.decode('utf-8').strip())

    show_memleaks = True

    # A negative return code means the process exited with a signal so do not
    # check for memory leaks in this case as it adds noise, right when it's
    # least wanted.
    if rc.returncode < 0:
        show_memleaks = False

    rc.fi_loc = log_test(conf,
                         log_file.name,
                         show_memleaks=show_memleaks)
    vh.convert_xml()
    return rc

def create_cont(conf, pool, posix=False):
    """Create a container and return the uuid"""
    if posix:
        cmd = ['container', 'create', '--pool', pool, '--type', 'POSIX']
    else:
        cmd = ['container', 'create', '--pool', pool]
    rc = run_daos_cmd(conf, cmd)
    print('rc is {}'.format(rc))
    assert rc.returncode == 0 # nosec
    assert rc.returncode == 0
    return rc.stdout.decode().split(' ')[-1].rstrip()

def destroy_container(conf, pool, container):
    """Destroy a container"""
    cmd = ['container', 'destroy', '--pool', pool, '--cont', container]
    rc = run_daos_cmd(conf, cmd)
    print('rc is {}'.format(rc))
    assert rc.returncode == 0 # nosec
    return rc.stdout.decode('utf-8').strip()

def make_pool(daos):
    """Create a DAOS pool"""

    size = int(daos.mb / 4)

    attempt = 0
    max_tries = 5
    while attempt < max_tries:
        rc = daos.run_dmg(['pool',
                           'create',
                           '--scm-size',
                           '{}M'.format(size)])
        if rc.returncode == 0:
            break
        attempt += 1
        time.sleep(0.5)

    print(rc)
    assert rc.returncode == 0 # nosec

    return get_pool_list()

def needs_dfuse(method):
    """Decorator function for starting dfuse under posix_tests class"""
    @functools.wraps(method)
    def _helper(self):
        self.dfuse = DFuse(self.server,
                           self.conf,
                           pool=self.pool,
                           container=self.container)
        self.dfuse.start(v_hint=method.__name__)
        rc = method(self)
        if self.dfuse.stop():
            self.fatal_errors = True
        return rc
    return _helper

def needs_dfuse_with_cache(method):
    """Decorator function for starting dfuse under posix_tests class"""
    @functools.wraps(method)
    def _helper(self):
        self.dfuse = DFuse(self.server,
                           self.conf,
                           pool=self.pool,
                           caching=True,
                           container=self.container)
        self.dfuse.start(v_hint=method.__name__)
        rc = method(self)
        if self.dfuse.stop():
            self.fatal_errors = True
        return rc
    return _helper

class posix_tests():
    """Class for adding standalone unit tests"""

    def __init__(self, server, conf, pool=None, container=None):
        self.server = server
        self.conf = conf
        self.pool = pool
        self.container = container
        self.dfuse = None
        self.fatal_errors = False

    # pylint: disable=no-self-use
    def fail(self):
        """Mark a test method as failed"""
        raise NLTestFail

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
            fd = open(os.path.join(wide_dir, str(idx)), 'w')
            fd.close()
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

    @needs_dfuse
    def test_open_replaced(self):
        """Test that fstat works on file clobbered by rename"""
        fname = os.path.join(self.dfuse.dir, 'unlinked')
        newfile = os.path.join(self.dfuse.dir, 'unlinked2')
        ofd = open(fname, 'w')
        nfd = open(newfile, 'w')
        nfd.write('hello')
        nfd.close()
        print(os.fstat(ofd.fileno()))
        os.rename(newfile, fname)
        # This should fail, because the file has been deleted.
        try:
            print(os.fstat(ofd.fileno()))
            self.fail()
        except FileNotFoundError:
            print('Failed to fstat() replaced file')
        ofd.close()

    @needs_dfuse
    def test_open_rename(self):
        """Check that fstat() on renamed files works as expected"""
        fname = os.path.join(self.dfuse.dir, 'unlinked')
        newfile = os.path.join(self.dfuse.dir, 'unlinked2')
        ofd = open(fname, 'w')
        pre = os.fstat(ofd.fileno())
        print(pre)
        os.rename(fname, newfile)
        try:
            post = os.fstat(ofd.fileno())
            print(post)
            self.fail()
        except FileNotFoundError:
            print('Failed to fstat() renamed file')
        os.stat(newfile)
        post = os.fstat(ofd.fileno())
        print(post)
        assert pre.st_ino == post.st_ino
        ofd.close()

    @needs_dfuse
    def test_open_unlinked(self):
        """Test that fstat works on unlinked file"""
        fname = os.path.join(self.dfuse.dir, 'unlinked')
        ofd = open(fname, 'w')
        print(os.fstat(ofd.fileno()))
        os.unlink(fname)
        try:
            print(os.fstat(ofd.fileno()))
            self.fail()
        except FileNotFoundError:
            print('Failed to fstat() unlinked file')
        ofd.close()

    @needs_dfuse
    def test_xattr(self):
        """Perform basic tests with extended attributes"""

        new_file = os.path.join(self.dfuse.dir, 'attr_file')
        fd = open(new_file, 'w')

        xattr.set(fd, 'user.mine', 'init_value')
        # This should fail as a security test.
        try:
            xattr.set(fd, 'user.dfuse.ids', b'other_value')
            assert False
        except OSError as e:
            assert e.errno == errno.EPERM

        try:
            xattr.set(fd, 'user.dfuse', b'other_value')
            assert False
        except OSError as e:
            assert e.errno == errno.EPERM

        xattr.set(fd, 'user.Xfuse.ids', b'other_value')
        for (key, value) in xattr.get_all(fd):
            print('xattr is {}:{}'.format(key, value))
        fd.close()

    @needs_dfuse
    def test_chmod(self):
        """Test that chmod works on file"""
        fname = os.path.join(self.dfuse.dir, 'testfile')
        ofd = open(fname, 'w')
        ofd.close()

        modes = [stat.S_IRUSR | stat.S_IWUSR | stat.S_IXUSR,
                 stat.S_IRUSR]

        for mode in modes:
            os.chmod(fname, mode)
            attr = os.stat(fname)
            assert stat.S_IMODE(attr.st_mode) == mode

    @needs_dfuse
    def test_fchmod_replaced(self):
        """Test that fchmod works on file clobbered by rename"""
        fname = os.path.join(self.dfuse.dir, 'unlinked')
        newfile = os.path.join(self.dfuse.dir, 'unlinked2')
        e_mode = stat.S_IRUSR | stat.S_IWUSR | stat.S_IXUSR
        ofd = open(fname, 'w')
        nfd = open(newfile, 'w')
        nfd.write('hello')
        nfd.close()
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
        ofd.close()
        nf = os.stat(fname)
        assert stat.S_IMODE(nf.st_mode) == e_mode

    @needs_dfuse
    def test_uns_create(self):
        """Simple test to create a container using a path in dfuse"""
        path = os.path.join(self.dfuse.dir, 'mycont')
        cmd = ['container', 'create',
               '--pool', self.pool, '--path', path,
               '--type', 'POSIX']
        rc = run_daos_cmd(self.conf, cmd)
        assert rc.returncode == 0
        stbuf = os.stat(path)
        print(stbuf)
        assert stbuf.st_ino < 100
        print(os.listdir(path))

    @needs_dfuse_with_cache
    def test_uns_create_with_cache(self):
        """Simple test to create a container using a path in dfuse"""
        path = os.path.join(self.dfuse.dir, 'mycont2')
        cmd = ['container', 'create',
               '--pool', self.pool, '--path', path,
               '--type', 'POSIX']
        rc = run_daos_cmd(self.conf, cmd)
        assert rc.returncode == 0
        stbuf = os.stat(path)
        print(stbuf)
        assert stbuf.st_ino < 100
        print(os.listdir(path))

    def test_uns_basic(self):
        """Create a UNS entry point and access it via both EP and path"""

        pool = self.pool
        container = self.container
        server = self.server
        conf = self.conf

        # Start dfuse on the container.
        dfuse = DFuse(server, conf, pool=pool, container=container)
        dfuse.start('uns-0')

        # Create a new container within it using UNS
        uns_path = os.path.join(dfuse.dir, 'ep0')
        uns_container = str(uuid.uuid4())
        cmd = ['container', 'create',
               '--pool', pool, '--cont', uns_container, '--path', uns_path,
               '--type', 'POSIX']

        print('Inserting entry point')
        rc = run_daos_cmd(conf, cmd)
        print('rc is {}'.format(rc))
        print(os.stat(uns_path))
        print(os.listdir(dfuse.dir))

        # Verify that it exists.
        run_container_query(conf, uns_path)

        # Make a directory in the new container itself, and query that.
        child_path = os.path.join(uns_path, 'child')
        os.mkdir(child_path)
        run_container_query(conf, child_path)
        if dfuse.stop():
            self.fatal_errors = True

        print('Trying UNS')
        dfuse = DFuse(server, conf)
        dfuse.start('uns-1')

        # List the root container.
        print(os.listdir(os.path.join(dfuse.dir, pool, container)))

        # Now create a UNS link from the 2nd container to a 3rd one.
        uns_path = os.path.join(dfuse.dir, pool, container, 'ep0', 'ep')
        second_path = os.path.join(dfuse.dir, pool, uns_container)

        uns_container = str(uuid.uuid4())

        # Make a link within the new container.
        cmd = ['container', 'create',
               '--pool', pool, '--cont', uns_container,
               '--path', uns_path, '--type', 'POSIX']

        print('Inserting entry point')
        rc = run_daos_cmd(conf, cmd)
        print('rc is {}'.format(rc))

        # List the root container again.
        print(os.listdir(os.path.join(dfuse.dir, pool, container)))

        # List the 2nd container.
        files = os.listdir(second_path)
        print(files)
        # List the target container through UNS.
        print(os.listdir(uns_path))
        direct_stat = os.stat(os.path.join(second_path, 'ep'))
        uns_stat = os.stat(uns_path)
        print(direct_stat)
        print(uns_stat)
        assert uns_stat.st_ino == direct_stat.st_ino # nosec

        third_path = os.path.join(dfuse.dir, pool, uns_container)
        third_stat = os.stat(third_path)
        print(third_stat)
        assert third_stat.st_ino == direct_stat.st_ino # nosec

        if dfuse.stop():
            self.fatal_errors = True
        print('Trying UNS with previous cont')
        dfuse = DFuse(server, conf)
        dfuse.start('uns-3')

        files = os.listdir(second_path)
        print(files)
        print(os.listdir(uns_path))

        direct_stat = os.stat(os.path.join(second_path, 'ep'))
        uns_stat = os.stat(uns_path)
        print(direct_stat)
        print(uns_stat)
        assert uns_stat.st_ino == direct_stat.st_ino # nosec
        if dfuse.stop():
            self.fatal_errors = True

def run_posix_tests(server, conf, test=None):
    """Run one or all posix tests"""

    def _run_test():
        start = time.time()
        print('Calling {}'.format(fn))
        rc = obj()
        duration = time.time() - start
        print('rc from {} is {}'.format(fn, rc))
        print('Took {:.1f} seconds'.format(duration))

    pools = get_pool_list()
    while len(pools) < 1:
        pools = make_pool(server)
    pool = pools[0]
    container = create_cont(conf, pool, posix=True)

    pt = posix_tests(server, conf, pool=pool, container=container)
    if test:
        fn = 'test_{}'.format(test)
        obj = getattr(pt, fn)
        _run_test()
    else:

        for fn in sorted(dir(pt)):
            if not fn.startswith('test'):
                continue
            obj = getattr(pt, fn)
            if not callable(obj):
                continue
            _run_test()

    destroy_container(conf, pool, container)
    return pt.fatal_errors

def run_tests(dfuse):
    """Run some tests"""
    path = dfuse.dir

    fname = os.path.join(path, 'test_file3')

    rc = subprocess.run(['dd', 'if=/dev/zero', 'bs=16k', 'count=64',
                         'of={}'.format(os.path.join(path, 'dd_file'))])
    print(rc)
    assert rc.returncode == 0 # nosec
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
    assert ret.returncode == 0 # nosec
    ofd = os.open(fname, os.O_TRUNC)
    assert_file_size_fd(ofd, 0)
    os.close(ofd)
    symlink_name = os.path.join(path, 'symlink_src')
    symlink_dest = 'missing_dest'
    os.symlink(symlink_dest, symlink_name)
    assert symlink_dest == os.readlink(symlink_name) # nosec

    # Note that this doesn't test dfs because fuse will do a
    # lookup to check if the file exists rather than just trying
    # to create it.
    fname = os.path.join(path, 'test_file5')
    fd = os.open(fname, os.O_CREAT | os.O_EXCL)
    os.close(fd)
    try:
        fd = os.open(fname, os.O_CREAT | os.O_EXCL)
        os.close(fd)
        assert False
    except OSError as e:
        assert e.errno == errno.EEXIST
    os.unlink(fname)

    # DAOS-6238
    fname = os.path.join(path, 'test_file4')
    ofd = os.open(fname, os.O_CREAT | os.O_RDONLY | os.O_EXCL)
    assert_file_size_fd(ofd, 0)
    os.close(ofd)
    os.chmod(fname, stat.S_IRUSR)

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
        os.stat(os.path.join(dfuse.dir, 'no-file'))
        raise NLTestFail('file exists')
    except FileNotFoundError:
        pass

lp = None
lt = None

def setup_log_test(conf):
    """Setup and import the log tracing code"""

    # Try and pick this up from the src tree if possible.
    file_self = os.path.dirname(os.path.abspath(__file__))
    logparse_dir = os.path.join(file_self,
                                '../src/tests/ftest/cart/util')
    crt_mod_dir = os.path.realpath(logparse_dir)
    if crt_mod_dir not in sys.path:
        sys.path.append(crt_mod_dir)

    # Or back off to the install dir if not.
    logparse_dir = os.path.join(conf['PREFIX'],
                                'lib/daos/TESTING/ftest/cart')
    crt_mod_dir = os.path.realpath(logparse_dir)
    if crt_mod_dir not in sys.path:
        sys.path.append(crt_mod_dir)

    global lp
    global lt

    lp = __import__('cart_logparse')
    lt = __import__('cart_logtest')

    lt.wf = conf.wf

def compress_file(filename):
    """Compress a file using bz2 for space reasons"""
    small = bz2.BZ2Compressor()

    fd = open(filename, 'rb')

    nfd = open('{}.bz2'.format(filename), 'wb')
    lines = fd.read(64*1024)
    while lines:
        new_data = bz2.compress(lines)
        if new_data:
            nfd.write(new_data)
        lines = fd.read(64*1024)
    new_data = small.flush()
    if new_data:
        nfd.write(new_data)

    os.unlink(filename)

# https://stackoverflow.com/questions/1094841/get-human-readable-version-of-file-size
def sizeof_fmt(num, suffix='B'):
    """Return size as a human readable string"""
    for unit in ['', 'Ki', 'Mi', 'Gi', 'Ti', 'Pi', 'Ei', 'Zi']:
        if abs(num) < 1024.0:
            return "%3.1f%s%s" % (num, unit, suffix)
        num /= 1024.0
    return "%.1f%s%s" % (num, 'Yi', suffix)

def log_test(conf,
             filename,
             show_memleaks=True,
             quiet=False,
             skip_fi=False,
             fi_signal=None,
             leak_wf=None,
             check_read=False,
             check_write=False):
    """Run the log checker on filename, logging to stdout"""

    # Check if the log file has wrapped, if it has then log parsing checks do
    # not work correctly.
    if os.path.exists('{}.old'.format(filename)):
        raise Exception('Log file exceeded max size')
    fstat = os.stat(filename)
    if not quiet:
        print('Running log_test on {} {}'.format(filename,
                                                 sizeof_fmt(fstat.st_size)))

    log_iter = lp.LogIter(filename)

    lto = lt.LogTest(log_iter, quiet=quiet)

    lto.hide_fi_calls = skip_fi

    wf_list = [conf.wf]
    if leak_wf:
        wf_list.append(leak_wf)

    try:
        lto.check_log_file(abort_on_warning=True,
                           show_memleaks=show_memleaks,
                           leak_wf=leak_wf)
    except lt.LogCheckError:
        if lto.fi_location:
            for wf in wf_list:
                wf.explain(lto.fi_location,
                           os.path.basename(filename),
                           fi_signal)

    if skip_fi:
        if not show_memleaks:
            for wf in wf_list:
                wf.explain(lto.fi_location,
                           os.path.basename(filename),
                           fi_signal)
        if not lto.fi_triggered:
            compress_file(filename)
            raise NLTestNoFi

    functions = set()

    if check_read or check_write:
        for line in log_iter.new_iter():
            functions.add(line.function)

    if check_read and 'dfuse_read' not in functions:
        raise NLTestNoFunction('dfuse_read')

    if check_write and 'dfuse_write' not in functions:
        raise NLTestNoFunction('dfuse_write')

    compress_file(filename)

    if conf.max_log_size and fstat.st_size > conf.max_log_size:
        raise Exception('Max log size exceeded, {} > {}'\
                        .format(sizeof_fmt(fstat.st_size),
                                sizeof_fmt(conf.max_log_size)))

    return lto.fi_location

def set_server_fi(server):
    """Run the client code to set server params"""

    cmd_env = get_base_env()

    cmd_env['OFI_INTERFACE'] = 'eth0'
    cmd_env['CRT_PHY_ADDR_STR'] = 'ofi+sockets'
    vh = ValgrindHelper(server.conf)

    system_name = 'daos_server'

    exec_cmd = vh.get_cmd_prefix()

    agent_bin = os.path.join(server.conf['PREFIX'], 'bin', 'daos_agent')

    addr_dir = tempfile.TemporaryDirectory(prefix='dnt_addr_',)
    addr_file = os.path.join(addr_dir.name,
                             '{}.attach_info_tmp'.format(system_name))

    agent_cmd = [agent_bin,
                 '-i',
                 '-s',
                 server.agent_dir,
                 'dump-attachinfo',
                 '-o',
                 addr_file]

    rc = subprocess.run(agent_cmd, env=cmd_env)
    print(rc)
    assert rc.returncode == 0

    cmd = ['set_fi_attr',
           '--cfg_path',
           addr_dir.name,
           '--group-name',
           'daos_server',
           '--rank',
           '0',
           '--attr',
           '0,0,0,0,0']

    exec_cmd.append(os.path.join(server.conf['PREFIX'], 'bin', 'cart_ctl'))
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
                        stderr=subprocess.PIPE)
    print(rc)
    vh.convert_xml()
    log_test(server.conf, log_file.name)
    assert rc.returncode == 0
    return False # fatal_errors

def create_and_read_via_il(dfuse, path):
    """Create file in dir, write to and read
    through the interception library"""

    fname = os.path.join(path, 'test_file')
    ofd = open(fname, 'w')
    ofd.write('hello ')
    ofd.write('world\n')
    ofd.flush()
    assert_file_size(ofd, 12)
    print(os.fstat(ofd.fileno()))
    ofd.close()
    ret = il_cmd(dfuse, ['cat', fname], check_write=False)
    assert ret.returncode == 0 # nosec

def run_container_query(conf, path):
    """Query a path to extract container information"""

    cmd = ['container', 'query', '--path', path]

    rc = run_daos_cmd(conf, cmd)

    assert rc.returncode == 0 # nosec

    print(rc)
    output = rc.stdout.decode('utf-8')
    for line in output.splitlines():
        print(line)

def run_duns_overlay_test(server, conf):
    """Create a DUNS entry point, and then start fuse over it

    Fuse should use the pool/container IDs from the entry point,
    and expose the container.
    """

    pools = get_pool_list()
    while len(pools) < 1:
        pools = make_pool(server)

    parent_dir = tempfile.TemporaryDirectory(dir=conf.dfuse_parent_dir,
                                             prefix='dnt_uns_')

    uns_dir = os.path.join(parent_dir.name, 'uns_ep')

    rc = run_daos_cmd(conf, ['container',
                             'create',
                             '--pool',
                             pools[0],
                             '--type',
                             'POSIX',
                             '--path',
                             uns_dir])

    print('rc is {}'.format(rc))
    assert rc.returncode == 0 # nosec

    dfuse = DFuse(server, conf, path=uns_dir)

    dfuse.start(v_hint='uns-overlay')
    # To show the contents.
    # getfattr -d <file>

    # This should work now if the container was correctly found
    create_and_read_via_il(dfuse, uns_dir)

    return dfuse.stop()

def run_dfuse(server, conf):
    """Run several dfuse instances"""

    fatal_errors = BoolRatchet()

    pools = get_pool_list()
    while len(pools) < 1:
        pools = make_pool(server)

    dfuse = DFuse(server, conf)
    try:
        pre_stat = os.stat(dfuse.dir)
    except OSError:
        umount(dfuse.dir)
        raise
    container = str(uuid.uuid4())
    dfuse.start(v_hint='no_pool')
    print(os.statvfs(dfuse.dir))
    subprocess.run(['df', '-h'])
    subprocess.run(['df', '-i', dfuse.dir])
    print('Running dfuse with nothing')
    stat_and_check(dfuse, pre_stat)
    check_no_file(dfuse)
    for pool in pools:
        pool_stat = os.stat(os.path.join(dfuse.dir, pool))
        print('stat for {}'.format(pool))
        print(pool_stat)
        cdir = os.path.join(dfuse.dir, pool, container)
        os.mkdir(cdir)
        #create_and_read_via_il(dfuse, cdir)
    fatal_errors.add_result(dfuse.stop())

    uns_container = container

    container2 = str(uuid.uuid4())
    dfuse = DFuse(server, conf, pool=pools[0])
    pre_stat = os.stat(dfuse.dir)
    dfuse.start(v_hint='pool_only')
    print('Running dfuse with pool only')
    stat_and_check(dfuse, pre_stat)
    check_no_file(dfuse)
    cpath = os.path.join(dfuse.dir, container2)
    os.mkdir(cpath)
    cdir = os.path.join(dfuse.dir, container)
    create_and_read_via_il(dfuse, cdir)

    fatal_errors.add_result(dfuse.stop())

    dfuse = DFuse(server, conf, pool=pools[0], container=container)
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

def run_il_test(server, conf):
    """Run a basic interception library test"""

    pools = get_pool_list()

    # TODO:                       # pylint: disable=W0511
    # This doesn't work with two pools, partly related to
    # DAOS-5109 but there may be other issues.
    while len(pools) < 1:
        pools = make_pool(server)

    print('pools are ', ','.join(pools))

    dfuse = DFuse(server, conf)
    dfuse.start()

    dirs = []

    for p in pools:
        for _ in range(2):
            # Use a unique ID for each container to avoid DAOS-5109
            container = str(uuid.uuid4())

            d = os.path.join(dfuse.dir, p, container)
            try:
                print('Making directory {}'.format(d))
                os.mkdir(d)
            except FileExistsError:
                pass
            dirs.append(d)

    # Create a file natively.
    f = os.path.join(dirs[0], 'file')
    fd = open(f, 'w')
    fd.write('Hello')
    fd.close()
    # Copy it across containers.
    ret = il_cmd(dfuse, ['cp', f, dirs[-1]])
    assert ret.returncode == 0 # nosec

    # Copy it within the container.
    child_dir = os.path.join(dirs[0], 'new_dir')
    os.mkdir(child_dir)
    il_cmd(dfuse, ['cp', f, child_dir])
    assert ret.returncode == 0 # nosec

    # Copy something into a container
    ret = il_cmd(dfuse, ['cp', '/bin/bash', dirs[-1]], check_read=False)
    assert ret.returncode == 0 # nosec
    # Read it from within a container
    # TODO:                              # pylint: disable=W0511
    # change this to something else, md5sum uses fread which isn't
    # intercepted.
    ret = il_cmd(dfuse,
                 ['md5sum', os.path.join(dirs[-1], 'bash')],
                 check_read=False, check_write=False)
    assert ret.returncode == 0 # nosec
    ret = il_cmd(dfuse, ['dd',
                         'if={}'.format(os.path.join(dirs[-1], 'bash')),
                         'of={}'.format(os.path.join(dirs[-1], 'bash_copy')),
                         'iflag=direct',
                         'oflag=direct',
                         'bs=128k'])

    print(ret)
    assert ret.returncode == 0 # nosec

    for my_dir in dirs:
        create_and_read_via_il(dfuse, my_dir)

    dfuse.stop()

def run_in_fg(server, conf):
    """Run dfuse in the foreground.

    Block until ctrl-c is pressed.
    """

    pools = get_pool_list()

    while len(pools) < 1:
        pools = make_pool(server)

    dfuse = DFuse(server, conf, pool=pools[0])
    dfuse.start()
    container = str(uuid.uuid4())
    t_dir = os.path.join(dfuse.dir, container)
    os.mkdir(t_dir)
    print('Running at {}'.format(t_dir))
    print('daos container create --type POSIX ' \
          '--pool {} --path {}/uns-link'.format(
              pools[0], t_dir))
    print('cd {}/uns-link'.format(t_dir))
    print('daos container destroy --path {}/uns-link'.format(t_dir))
    print('daos pool list-containers --pool {}'.format(pools[0]))
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
        dir_dir = os.path.join(parent,
                               'dirs.{}.in'.format(count))
        t_dir = os.path.join(parent,
                             'dirs.{}'.format(count))
        file_dir = os.path.join(parent,
                                'files.{}.in'.format(count))
        t_file = os.path.join(parent,
                              'files.{}'.format(count))

        start_all = time.time()
        if not os.path.exists(t_dir):
            try:
                os.mkdir(dir_dir)
            except FileExistsError:
                pass
            for i in range(count):
                try:
                    os.mkdir(os.path.join(dir_dir, str(i)))
                except FileExistsError:
                    pass
            dir_time = time.time() - start_all
            print('Creating {} dirs took {:.2f}'.format(count,
                                                        dir_time))
            os.rename(dir_dir, t_dir)

        if not os.path.exists(t_file):
            try:
                os.mkdir(file_dir)
            except FileExistsError:
                pass
            start = time.time()
            for i in range(count):
                f = open(os.path.join(file_dir, str(i)), 'w')
                f.close()
            file_time = time.time() - start
            print('Creating {} files took {:.2f}'.format(count,
                                                         file_time))
            os.rename(file_dir, t_file)

        return [dir_time, file_time]

    def print_results():
        """Display the results"""

        print(tabulate.tabulate(results,
                                headers=headers,
                                floatfmt=".2f"))

    pools = get_pool_list()

    while len(pools) < 1:
        pools = make_pool(server)

    pool = pools[0]

    container = str(uuid.uuid4())

    dfuse = DFuse(server, conf, pool=pool)

    print('Creating container and populating')
    count = 1024
    dfuse.start()
    parent = os.path.join(dfuse.dir, container)
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
        dfuse = DFuse(server, conf, pool=pool, container=container)
        dir_dir = os.path.join(dfuse.dir,
                               'dirs.{}'.format(count))
        file_dir = os.path.join(dfuse.dir,
                                'files.{}'.format(count))
        dfuse.start()
        start = time.time()
        subprocess.run(['/bin/ls', dir_dir], stdout=subprocess.PIPE)
        elapsed = time.time() - start
        print('processed {} dirs in {:.2f} seconds'.format(count,
                                                           elapsed))
        row.append(elapsed)
        dfuse.stop()
        dfuse = DFuse(server, conf, pool=pool, container=container)
        dfuse.start()
        start = time.time()
        subprocess.run(['/bin/ls', file_dir], stdout=subprocess.PIPE)
        elapsed = time.time() - start
        print('processed {} files in {:.2f} seconds'.format(count,
                                                            elapsed))
        row.append(elapsed)
        dfuse.stop()

        dfuse = DFuse(server, conf, pool=pool, container=container)
        dfuse.start()
        start = time.time()
        subprocess.run(['/bin/ls', '-t', dir_dir], stdout=subprocess.PIPE)
        elapsed = time.time() - start
        print('processed {} dirs in {:.2f} seconds'.format(count,
                                                           elapsed))
        row.append(elapsed)
        dfuse.stop()
        dfuse = DFuse(server, conf, pool=pool, container=container)
        dfuse.start()
        start = time.time()
        # Use sort by time here so ls calls stat, if you run ls -l then it will
        # also call getxattr twice which skews the figures.
        subprocess.run(['/bin/ls', '-t', file_dir], stdout=subprocess.PIPE)
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
        subprocess.run(['/bin/ls', '-t', file_dir], stdout=subprocess.PIPE)
        elapsed = time.time() - start
        print('processed {} files in {:.2f} seconds'.format(count,
                                                            elapsed))
        row.append(elapsed)
        start = time.time()
        subprocess.run(['/bin/ls', '-t', file_dir], stdout=subprocess.PIPE)
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
                        '--pool',
                        pool,
                        '--cont',
                        container])
    print_results()

def test_pydaos_kv(server, conf):
    """Test the KV interface"""

    pydaos_log_file = tempfile.NamedTemporaryFile(prefix='dnt_pydaos_',
                                                  suffix='.log',
                                                  delete=False)

    os.environ['D_LOG_FILE'] = pydaos_log_file.name
    daos = import_daos(server, conf)

    pools = get_pool_list()

    while len(pools) < 1:
        pools = make_pool(server)

    pool = pools[0]

    c_uuid = create_cont(conf, pool)

    container = daos.Cont(pool, c_uuid)

    kv = container.get_kv_by_name('my_test_kv', create=True)
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
    kv = container.get_kv_by_name('my_test_kv')
    kv = None
    container = None
    daos._cleanup()
    log_test(conf, pydaos_log_file.name)

# Fault injection testing.
#
# This runs two different commands under fault injection, although it allows
# for more to be added.  The command is defined, then run in a loop with
# different locations (loc) enabled, essentially failing each call to
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
# If a particular loc caused the command to exit with a signal then that
# location is re-run at the end under valgrind to get better diagnostics.
#

class AllocFailTestRun():
    """Class to run a fault injection command with a single fault"""

    def __init__(self, aft, cmd, env, loc):

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

        prefix = 'dnt_fi_check_{}_'.format(get_inc_id())
        self.log_file = tempfile.NamedTemporaryFile(prefix=prefix,
                                                    suffix='.log',
                                                    delete=False).name
        self.env['D_LOG_FILE'] = self.log_file

    def __str__(self):
        res = "Fault injection test of '{}'\n".format(' '.join(self.cmd))
        res += 'Fault injection location {}\n'.format(self.loc)
        if self.vh:
            res += 'Valgrind enabled for this test'
        if self.returncode is None:
            res += 'Process not completed'
        else:
            res += 'Returncode was {}'.format(self.returncode)
        return res

    def start(self):
        """Start the command"""
        fc = {}
        if self.loc:
            fc['fault_config'] = [{'id': 0,
                                   'probability_x': 1,
                                   'probability_y': 1,
                                   'interval': self.loc,
                                   'max_faults': 1}]

            self._fi_file = tempfile.NamedTemporaryFile(prefix='fi_',
                                                        suffix='.yaml')

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

        self.returncode = rc
        self.stdout = self._sp.stdout.read()
        self.stderr = self._sp.stderr.read()

        if self.stderr != b'':
            print('Stderr from command')
            print(self.stderr.decode('utf-8').strip())

        show_memleaks = True

        fi_signal = None
        # A negative return code means the process exited with a signal so do
        # not check for memory leaks in this case as it adds noise, right when
        # it's least wanted.
        if rc < 0:
            show_memleaks = False
            fi_signal = -rc

        try:
            self.fi_loc = log_test(self.aft.conf,
                                   self.log_file,
                                   show_memleaks=show_memleaks,
                                   quiet=True,
                                   skip_fi=True,
                                   leak_wf=self.aft.wf,
                                   fi_signal=fi_signal)
            self.fault_injected = True
        except NLTestNoFi:
            # If a fault wasn't injected then check output is as expected.
            # It's not possible to log these as warnings, because there is
            # no src line to log them against, so simply assert.
            assert self.returncode == 0
            assert self.stderr == b''
            if self.aft.expected_stdout is not None:
                assert self.stdout == self.aft.expected_stdout
            self.fault_injected = False
        if self.vh:
            self.vh.convert_xml()
        if not self.fault_injected:
            return
        if not self.aft.check_stderr:
            return

        if self.returncode == 0:
            if self.stdout != self.aft.expected_stdout:
                self.aft.wf.add(self.fi_loc,
                                'NORMAL',
                                "Incorrect stdout '{}'".format(self.stdout),
                                mtype='Out of memory caused zero exit '
                                'code with incorrect output')

        stderr = self.stderr.decode('utf-8').rstrip()
        if not stderr.endswith("Out of memory (-1009)") and \
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
                            mtype='Out of memory not reported '
                            'correctly via stderr')

class AllocFailTest():
    """Class to describe fault injection command"""

    def __init__(self, conf, cmd):
        self.conf = conf
        self.cmd = cmd
        self.prefix = True
        self.check_stderr = False
        self.expected_stdout = None
        self.use_il = False
        self.wf = conf.wf

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

        active = []
        fid = 1
        max_count = 0
        finished = False

        # List of fids to re-run under valgrind.
        to_rerun = []

        fatal_errors = False

        while not finished or active:
            if len(active) < max_child and not finished:
                active.append(self._run_cmd(fid))
                fid += 1

                if len(active) > max_count:
                    max_count = len(active)

            # Now complete as many as have finished.
            while active and active[0].has_finished():
                ret = active.pop(0)
                print(ret)
                if ret.returncode < 0:
                    fatal_errors = True
                    to_rerun.append(ret.loc)

                if not ret.fault_injected:
                    print('Fault injection did not trigger, stopping')
                    finished = True

        print('Completed, fid {}'.format(fid))
        print('Max in flight {}'.format(max_count))

        for fid in to_rerun:
            rerun = self._run_cmd(fid, valgrind=True)
            print(rerun)
            rerun.wait()

        return fatal_errors

    def _run_cmd(self,
                 loc,
                 valgrind=False):
        """Run the test with FI enabled
        """

        cmd_env = get_base_env()

        if self.use_il:
            cmd_env['LD_PRELOAD'] = os.path.join(self.conf['PREFIX'],
                                                 'lib64', 'libioil.so')

        cmd_env['DAOS_AGENT_DRPC_DIR'] = self.conf.agent_dir

        aftf = AllocFailTestRun(self, self.cmd, cmd_env, loc)
        if valgrind:
            aftf.vh = ValgrindHelper(self.conf)
            # Turn off leak checking in this case, as we're just interested in
            # why it crashed.
            aftf.vh.full_check = False

        aftf.start()

        return aftf

def test_alloc_fail_cat(server, conf, wf):
    """Run the Interception library with fault injection

    Start dfuse for this test, and do not do output checking on the command
    itself yet.
    """

    pools = get_pool_list()

    while len(pools) < 1:
        pools = make_pool(server)

    pool = pools[0]

    dfuse = DFuse(server, conf, pool=pool)
    dfuse.use_valgrind = False
    dfuse.start()

    container = str(uuid.uuid4())

    os.mkdir(os.path.join(dfuse.dir, container))
    target_file = os.path.join(dfuse.dir, container, 'test_file')

    fd = open(target_file, 'w')
    fd.write('Hello there')
    fd.close()

    cmd = ['cat', target_file]

    test_cmd = AllocFailTest(conf, cmd)
    test_cmd.use_il = True
    test_cmd.wf = wf

    rc = test_cmd.launch()
    dfuse.stop()
    return rc

def test_alloc_fail(server, conf):
    """run 'daos' client binary with fault injection"""

    pools = get_pool_list()

    while len(pools) < 1:
        pools = make_pool(server)

    pool = pools[0]

    cmd = [os.path.join(conf['PREFIX'], 'bin', 'daos'),
           'pool',
           'list-containers',
           '--pool',
           pool]
    test_cmd = AllocFailTest(conf, cmd)

    # Create at least one container, and record what the output should be when
    # the command works.
    container = create_cont(conf, pool)
    test_cmd.check_stderr = True

    rc = test_cmd.launch()
    destroy_container(conf, pool, container)
    return rc

def main():
    """Main entry point"""

    parser = argparse.ArgumentParser(
        description='Run DAOS client on local node')
    parser.add_argument('--server-debug', default=None)
    parser.add_argument('--dfuse-debug', default=None)
    parser.add_argument('--memcheck', default='some',
                        choices=['yes', 'no', 'some'])
    parser.add_argument('--max-log-size', default=None)
    parser.add_argument('--dfuse-dir', default='/tmp',
                        help='parent directory for all dfuse mounts')
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
        return

    conf = load_conf(args)

    wf = WarningsFactory('nlt-errors.json')

    wf_server = WarningsFactory('nlt-server-leaks.json')
    wf_client = WarningsFactory('nlt-client-leaks.json')

    conf.set_wf(wf)
    conf.set_args(args)
    setup_log_test(conf)

    server = DaosServer(conf)
    server.start()

    fatal_errors = BoolRatchet()
    fi_test = False

    if args.mode == 'launch':
        run_in_fg(server, conf)
    elif args.mode == 'il':
        fatal_errors.add_result(run_il_test(server, conf))
    elif args.mode == 'kv':
        test_pydaos_kv(server, conf)
    elif args.mode == 'overlay':
        fatal_errors.add_result(run_duns_overlay_test(server, conf))
    elif args.mode == 'set-fi':
        fatal_errors.add_result(set_server_fi(server))
    elif args.mode == 'fi':
        fi_test = True
    elif args.mode == 'all':
        fi_test = True
        fatal_errors.add_result(run_il_test(server, conf))
        fatal_errors.add_result(run_dfuse(server, conf))
        fatal_errors.add_result(run_duns_overlay_test(server, conf))
        fatal_errors.add_result(run_posix_tests(server, conf))
        test_pydaos_kv(server, conf)
        fatal_errors.add_result(set_server_fi(server))
    elif args.test:
        if args.test == 'all':
            fatal_errors.add_result(run_posix_tests(server, conf))
        else:
            fatal_errors.add_result(run_posix_tests(server, conf, args.test))
    else:
        fatal_errors.add_result(run_il_test(server, conf))
        fatal_errors.add_result(run_dfuse(server, conf))
        fatal_errors.add_result(run_posix_tests(server, conf))
        fatal_errors.add_result(set_server_fi(server))

    if server.stop(wf_server) != 0:
        fatal_errors.fail()

    # If running all tests then restart the server under valgrind.
    # This is really, really slow so just do list-containers, then
    # exit again.
    if args.mode == 'server-valgrind':
        server = DaosServer(conf, valgrind=True)
        server.start()
        pools = get_pool_list()
        for pool in pools:
            cmd = ['pool', 'list-containers', '--pool', pool]
            run_daos_cmd(conf, cmd, valgrind=False)
        if server.stop(wf_server) != 0:
            fatal_errors.add_result(True)

    # If the perf-check option is given then re-start everything without much
    # debugging enabled and run some microbenchmarks to give numbers for use
    # as a comparison against other builds.
    if args.perf_check or fi_test:
        args.server_debug = 'INFO'
        args.memcheck = 'no'
        args.dfuse_debug = 'WARN'
        server = DaosServer(conf)
        server.start()
        if fi_test:
            fatal_errors.add_result(test_alloc_fail_cat(server,
                                                        conf, wf_client))
            fatal_errors.add_result(test_alloc_fail(server, conf))
        if args.perf_check:
            check_readdir_perf(server, conf)
        if server.stop(wf_server) != 0:
            fatal_errors.fail()

    wf.close()
    wf_server.close()
    wf_client.close()
    if fatal_errors.errors:
        print("Significant errors encountered")
        sys.exit(1)

if __name__ == '__main__':
    main()
