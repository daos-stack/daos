#!/usr/bin/python3

"""Test code for dfuse"""

import os
import time
import uuid
import json
import signal
import subprocess
import tempfile

class DFTestFail(Exception):
    """Used to indicate test failure"""
    pass

def umount(path):
    """Umount dfuse from a given path"""
    cmd = ['fusermount', '-u', path]
    ret = subprocess.run(cmd)
    print('rc from umount {}'.format(ret.returncode))
    return ret.returncode

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
    return conf

class DaosServer():
    """Manage a DAOS server instance"""

    def __init__(self, conf):

        self._sp = None
        self._conf = conf
        self._agent = None
        self.tmp_dir = '/tmp'

    def start(self):
        """Start a DAOS server"""
        orterun = os.path.join(self._conf['OMPI_PREFIX'], 'bin', 'orterun')
        daos_server = os.path.join(self._conf['PREFIX'], 'bin', 'daos_server')
        config_file = os.path.join(self._conf['PREFIX'], 'etc', 'daos_server.yml.works')

        cmd = [orterun, '-n', '1', daos_server,
               '--config={}'.format(config_file), 'start', '-t' '4', '--insecure', '-a',
               self.tmp_dir]
        my_env = os.environ.copy()
        my_env['CRT_PHY_ADDR_STR'] = 'ofi+sockets'
        my_env['OFI_INTERFACE'] = 'lo'
        my_env['D_LOG_MASK'] = 'INFO'
        self._sp = subprocess.Popen(cmd, env=my_env)

        time.sleep(2)

        agent_bin = os.path.join(self._conf['PREFIX'], 'bin', 'daos_agent')

        self._agent = subprocess.Popen([agent_bin, '-i'])

    def __del__(self):
        """Stop a previously started DAOS server"""
        self._agent.send_signal(signal.SIGTERM)
        ret = self._agent.wait(timeout=5)
        print('rc from agent is {}'.format(ret))

        self._sp.send_signal(signal.SIGTERM)
        ret = self._sp.wait(timeout=5)
        print('rc from server is {}'.format(ret))

class DFuse():
    """Manage a dfuse instance"""
    def __init__(self, daos, conf, pool=None, container=None):
        self.dir = '/tmp/dfs_test'
        self.pool = pool
        self.container = container
        self._conf = conf
        self._daos = daos
        self._sp = None

        log_file = tempfile.NamedTemporaryFile(prefix='dfuse_', suffix='.log', delete=False)
        self.log_file = log_file.name

    def start(self):
        """Start a dfuse instance"""
        dfuse_bin = os.path.join(self._conf['PREFIX'], 'bin', 'dfuse')
        my_env = os.environ.copy()

        my_env['CRT_PHY_ADDR_STR'] = 'ofi+sockets'
        my_env['OFI_INTERFACE'] = 'lo'
        my_env['D_LOG_MASK'] = 'INFO,DFUSE=DEBUG'
        my_env['DD_MASK'] = 'all'
        my_env['DAOS_SINGLETON_CLI'] = '1'
        my_env['CRT_ATTACH_INFO_PATH'] = self._daos.tmp_dir
        my_env['D_LOG_FILE'] = self.log_file

        cmd = [dfuse_bin, '-s', '0', '-m', self.dir, '-f']
        if self.pool:
            cmd.extend(['-p', self.pool])
        if self.container:
            cmd.extend(['-c', self.container])
        self._sp = subprocess.Popen(cmd, env=my_env)
        time.sleep(2)
        print('Started dfuse at {}'.format(self.dir))
        print('Log file is {}'.format(self.log_file))

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
        """Stop a previously started dfuse instance"""
        if not self._sp:
            return

        print('Stopping fuse')
        ret = umount(self.dir)
        if ret:
            self._close_files()
            umount(self.dir)

        ret = self._sp.wait(timeout=5)
        print('rc from dfuse {}'.format(ret))
        self._sp = None

def make_pool(daos, conf):
    """Make a pool in DAOS"""
    daos_bin = os.path.join(conf['PREFIX'], 'bin', 'daos_shell')
    cmd = [daos_bin, '-i', 'pool', 'create', '-s', '1G']
    my_env = os.environ.copy()

    my_env['CRT_PHY_ADDR_STR'] = 'ofi+sockets'
    my_env['OFI_INTERFACE'] = 'lo'
    my_env['DAOS_SINGLETON_CLI'] = '1'
    my_env['CRT_ATTACH_INFO_PATH'] = daos.tmp_dir
    ret = subprocess.run(cmd, env=my_env)
    if ret.returncode != 0:
        raise Exception('Could not make pool')

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

def get_pool_names(daos, conf):
    """Return a list of valid pool names, create one if required"""
    pools = get_pool_list()
    if not pools:
        make_pool(daos, conf)
        pools = get_pool_list()
        if not pools:
            raise DFTestFail('No pools exist')
    return pools

def assert_file_size(ofd, size):
    """Verify the file size is as expected"""
    stat = os.fstat(ofd.fileno())
    print('Checking file size is {} {}'.format(size, stat.st_size))
    assert stat.st_size == size

def run_tests(dfuse):
    """Run some tests"""
    path = dfuse.dir

    fname = os.path.join(path, 'test_file')
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
    print(os.fstat(ofd.fileno()))
    ofd.close()

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

def run_dfuse(daos, conf):
    """Run several dfuse instances"""
    pools = get_pool_names(daos, conf)
    dfuse = DFuse(daos, conf)
    try:
        pre_stat = os.stat(dfuse.dir)
    except OSError:
        umount(dfuse.dir)
        raise
    dfuse.start()
    stat_and_check(dfuse, pre_stat)
    check_no_file(dfuse)
    for pool in pools:
        pool_stat = os.stat(os.path.join(dfuse.dir, pool))
        print('stat for {}'.format(pool))
        print(pool_stat)
    dfuse = None

    container = str(uuid.uuid4())
    dfuse = DFuse(daos, conf, pool=pools[0])
    pre_stat = os.stat(dfuse.dir)
    dfuse.start()

    stat_and_check(dfuse, pre_stat)
    check_no_file(dfuse)
    cpath = os.path.join(dfuse.dir, container)
    os.mkdir(cpath)
    cstat = os.stat(cpath)
    print(cstat)

    dfuse = None

    dfuse = DFuse(daos, conf, pool=pools[0], container=container)
    pre_stat = os.stat(dfuse.dir)
    dfuse.start()
    stat_and_check(dfuse, pre_stat)
    run_tests(dfuse)
    print('Reached the end, no errors')

def main():
    """Main entry point"""
    conf = load_conf()
    daos = DaosServer(conf)
    daos.start()
    run_dfuse(daos, conf)

if __name__ == '__main__':
    main()
