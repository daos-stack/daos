#!/usr/bin/python3

"""Test code for dfuse"""

import os
import sys
import time
import uuid
import yaml
import json
import signal
import subprocess
import tempfile
import pickle

from collections import OrderedDict

class DFTestFail(Exception):
    """Used to indicate test failure"""
    pass

class DFTestNoFi(DFTestFail):
    """Used to indicate Fault injection didn't work"""
    pass

instance_num = 0

def get_inc_id():
    """Return a unique character"""
    global instance_num
    instance_num += 1
    return str(instance_num)

def umount(path):
    """Umount dfuse from a given path"""
    cmd = ['fusermount3', '-u', path]
    ret = subprocess.run(cmd)
    print('rc from umount {}'.format(ret.returncode))
    return ret.returncode

class NLT_Conf():
    """Helper class for configuration"""
    def __init__(self, bc):
        self.bc = bc
        self.agent_dir = None
        self.wf = None

    def set_wf(self, wf):
        """Set the WarningsFactory object"""
        self.wf = wf

    def __getitem__(self, key):
        return self.bc[key]

class WarningsFactory():
    """Class to parse warnings, and save to JSON output file

    Take a list of failures, and output the data in a way that is best
    displayed according to
    https://github.com/jenkinsci/warnings-ng-plugin/blob/master/doc/Documentation.md
    """

    # Error levels are LOW, NORMAL, HIGH, ERROR
    FLAKY_FUNCTIONS = ('daos_lru_cache_destroy', 'vos_tls_fini', 'rdb_timerd')

    def __init__(self, filename):
        self._fd = open(filename, 'w')
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
        entry['lineStart'] = sys._getframe().f_lineno
        entry['message'] = 'Tests exited without shutting down properly'
        entry['fingerprint'] = 'tests-died-while-running'
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
#        if line.filename == 'src/security/cli_security.c':
#            entry['fingerprint'] = '2DAC808D8D0018ECCBBE168CA02DBC1A'
#        elif line.filename == 'src/rdb/rdb_raft.c':
#            entry['filename'] = '972353F8A37813C69F01058745322AA1'
#        else:
#            if cat == 'Fault injection location':
#                entry['fingerprint'] = '{} {}'.format(line.filename, line.get_anon_msg())
#            else:
#                entry['fingerprint'] = '{} {}{}'.format(line.filename, line.get_anon_msg(), message)
        entry['severity'] = sev
        if line.function in self.FLAKY_FUNCTIONS and \
           entry['severity'] == 'NORMAL':
            entry['severity'] = 'LOW'
        self.issues.append(entry)
        self.pending.append((line, message))
        self._flush()

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
            entry['lineStart'] = sys._getframe().f_lineno
            entry['severity'] = 'ERROR'
            entry['message'] = 'Tests are still running'
            entry['fingerprint'] = 'tests-still-running'
            data['issues'].append(entry)
        json.dump(data, self._fd, indent=2)
        self._fd.flush()

    def close(self):
        """Save, and close the log file"""
        self._running = False
        self._flush()
        self._fd.close()
        self._fd = None
        print('Closed JSON file with {} errors'.format(len(self.issues)))

def load_conf():
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
    return NLT_Conf(conf)

def get_base_env():
    """Return the base set of env vars needed for DAOS"""

    env = os.environ.copy()
    env['DD_MASK'] = 'all'
    env['DD_SUBSYS'] = 'all'
    env['D_LOG_MASK'] = 'DEBUG'
    env['FI_UNIVERSE_SIZE'] = '128'
    return env

class DaosServer():
    """Manage a DAOS server instance"""

    def __init__(self, conf):
        self.running = False

        self._sp = None
        self.conf = conf
        self._agent = None
        self.agent_dir = None
        # Also specified in the yaml file.
        self._log_file = '/tmp/dnt_server.log'

        socket_dir = '/tmp/dnt_sockets'
        if not os.path.exists(socket_dir):
            os.mkdir(socket_dir)
        if os.path.exists(self._log_file):
            os.unlink(self._log_file)

        # self._agent_dir = tempfile.TemporaryDirectory(prefix='dnt_agent_')
        # Disable this for now, as the filename is logged without a error in
        # the fault injection testing so it's causing inconsisency in the
        # errors generated.
        # self._agent_dir = tempfile.TemporaryDirectory(prefix='dnt_agent_')
        # self.agent_dir = self._agent_dir.name

        self.agent_dir = '/tmp/dnt_agent_changeme'
        if not os.path.exists(self.agent_dir):
            os.mkdir(self.agent_dir)

    def __del__(self):
        if self.running:
            self.stop()

    def start(self):
        """Start a DAOS server"""

        daos_server = os.path.join(self.conf['PREFIX'], 'bin', 'daos_server')

        self_dir = os.path.dirname(os.path.abspath(__file__))

        server_config = os.path.join(self_dir, 'nlt_server.yaml')

        cmd = [daos_server, '--config={}'.format(server_config),
               'start', '-t' '4', '--insecure', '-d', self.agent_dir,
               '--recreate-superblocks']

        server_env = get_base_env()
        server_env['DAOS_DISABLE_REQ_FWD'] = '1'
        self._sp = subprocess.Popen(cmd, env=server_env)

        agent_config = os.path.join(self_dir, 'nlt_agent.yaml')

        agent_env = os.environ.copy()
        # DAOS-??? Need to set this for agent
        agent_env['LD_LIBRARY_PATH'] = os.path.join(self.conf['PREFIX'],
                                                    'lib64')
        agent_bin = os.path.join(self.conf['PREFIX'], 'bin', 'daos_agent')

        self._agent = subprocess.Popen([agent_bin,
                                        '--config-path', agent_config,
                                        '--insecure',
                                        '--debug',
                                        '--runtime_dir', self.agent_dir,
                                        '--logfile', '/tmp/dnt_agent.log'],
                                       env=agent_env)
        self.conf.agent_dir = self.agent_dir
        time.sleep(2)
        self.running = True

    def stop(self):
        """Stop a previously started DAOS server"""
        if self._agent:
            self._agent.send_signal(signal.SIGINT)
            ret = self._agent.wait(timeout=5)
            print('rc from agent is {}'.format(ret))

        if not self._sp:
            return

        # daos_server does not correctly shutdown daos_io_server yet
        # so find and kill daos_io_server directly.  This may cause
        # a assert in daos_io_server, but at least we can check that.
        # call daos_io_server, wait, and then call daos_server.
        # When parsing the server logs do not report on memory leaks
        # yet, as if it fails then lots of memory won't be freed and
        # it's not helpful at this stage to report that.
        # TODO: Remove this block when daos_server shutdown works.
        parent_pid = self._sp.pid
        for proc_id in os.listdir('/proc/'):
            if proc_id == 'self':
                continue
            status_file = '/proc/{}/status'.format(proc_id)
            if not os.path.exists(status_file):
                continue
            fd = open(status_file, 'r')
            this_proc = False
            for line in fd.readlines():
                try:
                    key, v = line.split(':', maxsplit=2)
                except ValueError:
                    continue
                value = v.strip()
                if key == 'Name' and value != 'daos_io_server':
                    break
                if key != 'PPid':
                    continue
                if int(value) == parent_pid:
                    this_proc = True
                    break
            if not this_proc:
                continue
            print('Target pid is {}'.format(proc_id))
            os.kill(int(proc_id), signal.SIGTERM)
            time.sleep(5)

        self._sp.send_signal(signal.SIGTERM)
        ret = self._sp.wait(timeout=5)
        print('rc from server is {}'.format(ret))
        # Show errors from server logs bug suppress memory leaks as the server
        # often segfaults at shutdown.
        if os.path.exists(self._log_file):
            # TODO: Enable memleak checking when server shutdown works.
            log_test(self.conf, self._log_file, show_memleaks=False)
        self.running = False
        return ret

def il_cmd(dfuse, cmd):
    """Run a command under the interception library"""
    my_env = get_base_env()
    prefix = 'dnt_dfuse_il_{}_'.format(get_inc_id())
    log_file = tempfile.NamedTemporaryFile(prefix=prefix,
                                           suffix='.log',
                                           delete=False)
    my_env['D_LOG_FILE'] = log_file.name
    my_env['LD_PRELOAD'] = os.path.join(dfuse.conf['PREFIX'],
                                        'lib64', 'libioil.so')
    ret = subprocess.run(cmd, env=my_env)
    print('Logged il to {}'.format(log_file.name))
    print(ret)
    print('Log results for il')
    log_test(dfuse.conf, log_file.name)
    return ret

class ValgrindHelper():

    """Class for running valgrind commands

    This helps setup the command line required, and
    performs log modification after the fact to assist
    Jenkins in locating the source code.
    """

    def __init__(self, logid=None):

        # Set this to False to disable valgrind, which will run faster.
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

        cmd = ['valgrind', '--quiet']

        if self.full_check:
            cmd.extend(['--leak-check=full', '--show-leak-kinds=all'])
        else:
            cmd.extend(['--leak-check=no'])

        s_arg = '--suppressions='
        cmd.extend(['{}{}'.format(s_arg,
                                  os.path.join('src',
                                               'cart',
                                               'utils',
                                               'memcheck-cart.supp')),
                    '{}{}'.format(s_arg,
                                  os.path.join('utils',
                                               'memcheck-daos-client.supp'))])

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

    def __init__(self, daos, conf, pool=None, container=None, path=None):
        if path:
            self.dir = path
        else:
            self.dir = '/tmp/dfs_test'
        self.pool = pool
        self.valgrind_file = None
        self.container = container
        self.conf = conf
        self._daos = daos
        self._sp = None

        prefix = 'dnt_dfuse_{}_'.format(get_inc_id())
        log_file = tempfile.NamedTemporaryFile(prefix=prefix,
                                               suffix='.log',
                                               delete=False)
        self.log_file = log_file.name

        self.valgrind = None
        if not os.path.exists(self.dir):
            os.mkdir(self.dir)

    def start(self, v_hint=None):
        """Start a dfuse instance"""
        dfuse_bin = os.path.join(self.conf['PREFIX'], 'bin', 'dfuse')

        single_threaded = False
        caching = False

        pre_inode = os.stat(self.dir).st_ino

        my_env = get_base_env()

        my_env['D_LOG_FILE'] = self.log_file
        my_env['DAOS_AGENT_DRPC_DIR'] = self._daos.agent_dir

        self.valgrind = ValgrindHelper(v_hint)
        cmd = self.valgrind.get_cmd_prefix()

        cmd.extend([dfuse_bin, '-s', '0', '-m', self.dir, '-f'])

        if single_threaded:
            cmd.append('-S')

        if caching:
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
            if total_time > 30:
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
        if not self._sp:
            return

        print('Stopping fuse')
        ret = umount(self.dir)
        if ret:
            self._close_files()
            umount(self.dir)

        try:
            ret = self._sp.wait(timeout=20)
            print('rc from dfuse {}'.format(ret))
        except subprocess.TimeoutExpired:
            self._sp.send_signal(signal.SIGTERM)
        self._sp = None
        log_test(self.conf, self.log_file)

        # Finally, modify the valgrind xml file to remove the
        # prefix to the src dir.
        self.valgrind.convert_xml()

    def wait_for_exit(self):
        """Wait for dfuse to exit"""
        ret = self._sp.wait()
        print('rc from dfuse {}'.format(ret))
        self._sp = None

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

def assert_file_size(ofd, size):
    """Verify the file size is as expected"""
    stat = os.fstat(ofd.fileno())
    print('Checking file size is {} {}'.format(size, stat.st_size))
    assert stat.st_size == size

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

    os.environ["DAOS_AGENT_DRPC_DIR"] = server.agent_dir

    daos = __import__('pydaos')
    return daos

def run_daos_cmd(conf, cmd, fi_file=None, fi_valgrind=False):
    """Run a DAOS command

    Run a command, returning what subprocess.run() would.

    Enable logging, and valgrind for the command.
    """
    valgrind = ValgrindHelper()

    if fi_file:
        # Turn off Valgrind for the fault injection testing unless it's
        # specifically requested (typically if a fault injection results
        # in a SEGV/assert), and then if it is turned on then just check
        # memory access, not memory leaks.
        valgrind.use_valgrind = fi_valgrind
        valgrind.full_check = False

    exec_cmd = valgrind.get_cmd_prefix()
    exec_cmd.append(os.path.join(conf['PREFIX'], 'bin', 'daos'))
    exec_cmd.extend(cmd)

    cmd_env = get_base_env()

    prefix = 'dnt_cmd_{}_'.format(get_inc_id())
    log_file = tempfile.NamedTemporaryFile(prefix=prefix,
                                           suffix='.log',
                                           delete=False)

    if fi_file:
        cmd_env['D_FI_CONFIG'] = fi_file
    cmd_env['D_LOG_FILE'] = log_file.name
    if conf.agent_dir:
        cmd_env['DAOS_AGENT_DRPC_DIR'] = conf.agent_dir

    rc = subprocess.run(exec_cmd,
                        stdout=subprocess.PIPE,
                        env=cmd_env)

    show_memleaks = True
    skip_fi = False

    if fi_file:
        skip_fi = True

    fi_signal = None
    # A negative return code means the process exited with a signal so do not
    # check for memory leaks in this case as it adds noise, right when it's
    # least wanted.
    if rc.returncode < 0:
        show_memleaks = False
        fi_signal = -rc.returncode

    log_test(conf,
             log_file.name,
             show_memleaks=show_memleaks,
             skip_fi=skip_fi,
             fi_signal=fi_signal)
    valgrind.convert_xml()
    return rc

def show_cont(conf, pool):
    """Create a container and return a container list"""
    cmd = ['container', 'create', '--svc', '0', '--pool', pool]
    rc = run_daos_cmd(conf, cmd)
    assert rc.returncode == 0
    print('rc is {}'.format(rc))

    cmd = ['pool', 'list-containers', '--svc', '0', '--pool', pool]
    rc = run_daos_cmd(conf, cmd)
    print('rc is {}'.format(rc))
    assert rc.returncode == 0
    return rc.stdout.strip()

def make_pool(daos, conf):
    """Create a DAOS pool"""

    time.sleep(2)

    daos_raw = __import__('pydaos.raw')

    context = daos.raw.DaosContext(os.path.join(conf['PREFIX'], 'lib64'))

    pool_con = daos.raw.DaosPool(context)

    try:
        pool_con.create(511, os.geteuid(), os.getegid(),
                        1024*1014*128, b'daos_server')
    except daos_raw.raw.daos_api.DaosApiError:
        time.sleep(10)
        pool_con.create(511, os.geteuid(), os.getegid(),
                        1024*1014*128, b'daos_server')
    return get_pool_list()

def run_tests(dfuse):
    """Run some tests"""
    path = dfuse.dir

    fname = os.path.join(path, 'test_file3')
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
    ofd.write('world\n')
    ofd.flush()
    assert_file_size(ofd, 6)
    print(os.fstat(ofd.fileno()))
    ofd.close()
    il_cmd(dfuse, ['cat', fname])

def stat_and_check(dfuse, pre_stat):
    """Check that dfuse started"""
    post_stat = os.stat(dfuse.dir)
    if pre_stat.st_dev == post_stat.st_dev:
        raise DFTestFail('Device # unchanged')
    if post_stat.st_ino != 1:
        raise DFTestFail('Unexpected inode number')

def check_no_file(dfuse):
    """Check that a non-existent file doesn't exist"""
    try:
        os.stat(os.path.join(dfuse.dir, 'no-file'))
        raise DFTestFail('file exists')
    except FileNotFoundError:
        pass

lp = None
lt = None

def setup_log_test(conf):
    """Setup and import the log tracing code"""
    file_self = os.path.dirname(os.path.abspath(__file__))
    logparse_dir = os.path.join(file_self,
                                '../src/cart/test/util')
    crt_mod_dir = os.path.realpath(logparse_dir)
    print(crt_mod_dir)
    if crt_mod_dir not in sys.path:
        sys.path.append(crt_mod_dir)

    global lp
    global lt

    lp = __import__('cart_logparse')
    lt = __import__('cart_logtest')

    lt.wf = conf.wf

def log_test(conf,
             filename,
             show_memleaks=True,
             skip_fi=False,
             fi_signal=None):
    """Run the log checker on filename, logging to stdout"""

    print('Running log_test on {}'.format(filename))

    log_iter = lp.LogIter(filename)
    lto = lt.LogTest(log_iter)

    lto.hide_fi_calls = skip_fi

    try:
        lto.check_log_file(abort_on_warning=True,
                           show_memleaks=show_memleaks)
    except lt.LogCheckError:
        if lto.fi_location:
            conf.wf.explain(lto.fi_location,
                            os.path.basename(filename),
                            fi_signal)

    if skip_fi:
        if not show_memleaks:
            conf.wf.explain(lto.fi_location,
                            os.path.basename(filename),
                            fi_signal)
        if not lto.fi_triggered:
            raise DFTestNoFi

def create_and_read_via_il(dfuse, path):
    """Create file in dir, write to and and read
    through the interception library"""

    fname = os.path.join(path, 'test_file')
    ofd = open(fname, 'w')
    ofd.write('hello ')
    ofd.write('world\n')
    ofd.flush()
    assert_file_size(ofd, 12)
    print(os.fstat(ofd.fileno()))
    ofd.close()
    il_cmd(dfuse, ['cat', fname])

def run_container_query(conf, path):
    """Query a path to extract container information"""

    cmd = ['container', 'query', '--svc', '0', '--path', path]

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
    daos = import_daos(server, conf)

    pools = get_pool_list()
    while len(pools) < 1:
        pools = make_pool(daos, conf)

    parent_dir = tempfile.TemporaryDirectory(prefix='dnt_uns_')

    uns_dir = os.path.join(parent_dir.name, 'uns_ep')

    rc = run_daos_cmd(conf, ['container',
                             'create',
                             '--svc',
                             '0',
                             '--pool',
                             pools[0],
                             '--type',
                             'POSIX',
                             '--path',
                             uns_dir])

    print('rc is {}'.format(rc))
    assert rc.returncode == 0

    dfuse = DFuse(server, conf, path=uns_dir)

    dfuse.start(v_hint='uns-overlay')
    # To show the contents.
    # getfattr -d <file>

    # This should work now if the container was correctly found
    create_and_read_via_il(dfuse, uns_dir)

    dfuse.stop()

def run_dfuse(server, conf):
    """Run several dfuse instances"""

    daos = import_daos(server, conf)

    pools = get_pool_list()
    while len(pools) < 1:
        pools = make_pool(daos, conf)

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
    dfuse.stop()

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

    dfuse.stop()

    dfuse = DFuse(server, conf, pool=pools[0], container=container)
    pre_stat = os.stat(dfuse.dir)
    dfuse.start(v_hint='pool_and_cont')
    print('Running fuse with both')

    stat_and_check(dfuse, pre_stat)

    create_and_read_via_il(dfuse, dfuse.dir)

    run_tests(dfuse)

    dfuse.stop()

    dfuse = DFuse(server, conf, pool=pools[0], container=container2)
    dfuse.start('uns-0')

    uns_path = os.path.join(dfuse.dir, 'ep0')

    uns_container = str(uuid.uuid4())

    cmd = ['container', 'create', '--svc', '0',
           '--pool', pools[0], '--cont', uns_container, '--path', uns_path,
           '--type', 'POSIX']

    print('Inserting entry point')
    rc = run_daos_cmd(conf, cmd)
    print('rc is {}'.format(rc))
    print(os.stat(uns_path))
    print(os.stat(uns_path))
    print(os.listdir(dfuse.dir))

    run_container_query(conf, uns_path)

    child_path = os.path.join(uns_path, 'child')
    os.mkdir(child_path)
    run_container_query(conf, child_path)

    dfuse.stop()

    print('Trying UNS')
    dfuse = DFuse(server, conf)
    dfuse.start('uns-2')

    # List the root container.
    print(os.listdir(os.path.join(dfuse.dir, pools[0], container2)))

    uns_path = os.path.join(dfuse.dir, pools[0], container2, 'ep0', 'ep')
    direct_path = os.path.join(dfuse.dir, pools[0], uns_container)

    uns_container = str(uuid.uuid4())

    # Make a link within the new container.
    cmd = ['container', 'create', '--svc', '0',
           '--pool', pools[0], '--cont', uns_container,
           '--path', uns_path, '--type', 'POSIX']

    print('Inserting entry point')
    rc = run_daos_cmd(conf, cmd)
    print('rc is {}'.format(rc))

    # List the root container again.
    print(os.listdir(os.path.join(dfuse.dir, pools[0], container2)))

    # List the target container.
    files = os.listdir(direct_path)
    print(files)
    # List the target container through UNS.
    print(os.listdir(uns_path))
    direct_stat = os.stat(os.path.join(direct_path, files[0]))
    uns_stat = os.stat(uns_path)
    print(direct_stat)
    print(uns_stat)
    assert uns_stat.st_ino == direct_stat.st_ino

    dfuse.stop()
    print('Trying UNS with previous cont')
    dfuse = DFuse(server, conf)
    dfuse.start('uns-3')

    files = os.listdir(direct_path)
    print(files)
    print(os.listdir(uns_path))

    direct_stat = os.stat(os.path.join(direct_path, files[0]))
    uns_stat = os.stat(uns_path)
    print(direct_stat)
    print(uns_stat)
    assert uns_stat.st_ino == direct_stat.st_ino
    dfuse.stop()

    print('Reached the end, no errors')

def run_il_test(server, conf):
    """Run a basic interception library test"""
    daos = import_daos(server, conf)

    pools = get_pool_list()

    # TODO: This doesn't work with two pools, there appears to be a bug
    # relating to re-using container uuids across pools.
    while len(pools) < 1:
        pools = make_pool(daos, conf)

    print('pools are ', ','.join(pools))

    containers = ['62176a51-8229-4e4c-ad1b-43aaace8a97a',
                  '4ef12a58-c544-406c-8acf-56a2c0589cd6']

    dfuse = DFuse(server, conf)
    dfuse.start()

    dirs = []

    for p in pools:
        for c in containers:
            d = os.path.join(dfuse.dir, p, c)
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
    il_cmd(dfuse, ['cp', f, dirs[-1]])

    # Copy it within the container.
    child_dir = os.path.join(dirs[0], 'new_dir')
    os.mkdir(child_dir)
    il_cmd(dfuse, ['cp', f, child_dir])

    # Copy something into a container
    il_cmd(dfuse, ['cp', '/bin/bash', dirs[-1]])
    # Read it from within a container
    il_cmd(dfuse, ['md5sum', os.path.join(dirs[-1], 'bash')])
    dfuse.stop()

def run_in_fg(server, conf):
    """Run dfuse in the foreground.

    Block until ctrl-c is pressed.
    """
    daos = import_daos(server, conf)

    pools = get_pool_list()

    while len(pools) < 1:
        pools = make_pool(daos, conf)

    dfuse = DFuse(server, conf, pool=pools[0])
    dfuse.start()
    container = str(uuid.uuid4())
    t_dir = os.path.join(dfuse.dir, container)
    os.mkdir(t_dir)
    print('Running at {}'.format(t_dir))
    print('daos container create --svc 0 --type POSIX' \
          '--pool {} --path {}/uns-link'.format(
              pools[0], t_dir))
    print('cd {}/uns-link'.format(t_dir))
    print('daos container destroy --svc 0 --path {}/uns-link'.format(t_dir))
    print('daos pool list-containers --svc 0 --pool {}'.format(pools[0]))
    try:
        dfuse.wait_for_exit()
    except KeyboardInterrupt:
        pass
    dfuse = None

def test_pydaos_kv(server, conf):
    """Test the KV interface"""

    daos = import_daos(server, conf)

    file_self = os.path.dirname(os.path.abspath(__file__))
    mod_dir = os.path.join(file_self,
                           '../src/client/pydaos')
    if mod_dir not in sys.path:
        sys.path.append(mod_dir)

    dbm = __import__('daosdbm')

    pools = get_pool_list()

    while len(pools) < 1:
        pools = make_pool(daos, conf)

    pool = pools[0]

    container = show_cont(conf, pool)

    print(container)
    c_uuid = container.decode().split()[-1]
    kvg = dbm.daos_named_kv(pool, c_uuid)

    kv = kvg.get_kv_by_name('Dave')
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

    kv.bget(data, value_size=16)
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

def test_alloc_fail(conf):
    """run 'daos' client binary with fault injection

    Enable the fault injection for the daos binary, injecting
    allocation failures at different locations.  Keep going until
    the client runs with no faults injected (about 800 iterations).

    Disable valgrind for this test as it takes a long time to run
    with valgrind enabled, use purely the log analysis to find issues.

    Ignore new error messages containing the numeric value of -DER_NOMEM
    but warn on all other warnings generated.
    """

    pools = get_pool_list()

    if len(pools) > 1:
        pool = pools[0]
    else:
        pool = '5848df55-a97c-46e3-8eca-45adf85591d6'

    cmd = ['pool', 'list-containers', '--svc', '0', '--pool', pool]

    fid = 1

    fatal_errors = False

    while True:
        fc = {}
        fc['fault_config'] = [{'id': 0,
                               'probability_x': 1,
                               'probability_y': 1,
                               'interval': fid,
                               'max_faults': 1}]

        fi_file = tempfile.NamedTemporaryFile(prefix='fi_',
                                              suffix='.yaml')

        fi_file.write(yaml.dump(fc, encoding='utf=8'))
        fi_file.flush()

        try:
            rc = run_daos_cmd(conf, cmd, fi_file=fi_file.name)
            if rc.returncode < 0:
                print(rc)
                print('Rerunning test under valgrind, fid={}'.format(fid))
                rc = run_daos_cmd(conf,
                                  cmd,
                                  fi_file=fi_file.name,
                                  fi_valgrind=True)
                fatal_errors = True
        except DFTestNoFi:
            print('Fault injection did not trigger, returning')
            break

        print(rc)
        fid += 1
        # Keep going until program runs to completion.  We should add checking
        # of exit code at some point, but it would need to be reported properly
        # through Jenkins.
        # if rc.returncode not in (1, 255):
        #   break
    return fatal_errors

def main():
    """Main entry point"""

    conf = load_conf()

    wf = WarningsFactory('nlt-errors.json')

    conf.set_wf(wf)
    setup_log_test(conf)

    server = DaosServer(conf)
    server.start()

    fatal_errors = False

    if len(sys.argv) == 2 and sys.argv[1] == 'launch':
        run_in_fg(server, conf)
    elif len(sys.argv) == 2 and sys.argv[1] == 'kv':
        test_pydaos_kv(server, conf)
    elif len(sys.argv) == 2 and sys.argv[1] == 'overlay':
        run_duns_overlay_test(server, conf)
    elif len(sys.argv) == 2 and sys.argv[1] == 'fi':
        fatal_errors = test_alloc_fail(conf)
    elif len(sys.argv) == 2 and sys.argv[1] == 'all':
        run_il_test(server, conf)
        run_dfuse(server, conf)
        run_duns_overlay_test(server, conf)
        test_pydaos_kv(server, conf)
        fatal_errors = test_alloc_fail(conf)
    else:
        run_il_test(server, conf)
        run_dfuse(server, conf)

    if server.stop() != 0:
        fatal_errors = True

    wf.close()
    if fatal_errors:
        print("Significant errors encountered")

if __name__ == '__main__':
    main()
