#!/usr/bin/python3

"""Test code for dfuse"""

import os
import sys
import time
import uuid
import json
import shelve
import shutil
import signal
import subprocess
import tempfile
import pickle

from collections import OrderedDict

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

#        symlink_file('/tmp/dfuse_server.latest.log', '/tmp/server.log')

        socket_dir = '/tmp/daos_sockets'
        if not os.path.exists(socket_dir):
            os.mkdir(socket_dir)
        if os.path.exists('/tmp/server.log'):
            os.unlink('/tmp/server.log')

    def start(self):
        """Start a DAOS server"""

        if True:
            for fname in os.listdir('/mnt/daos'):
                try:
                    shutil.rmtree(os.path.join('/mnt/daos', fname))
                except NotADirectoryError:
                    os.remove(os.path.join('/mnt/daos', fname))
        daos_server = os.path.join(self._conf['PREFIX'], 'bin', 'daos_server')
        config_file = os.path.expanduser(os.path.join('~', 'daos_server.yml.works'))

        cmd = [daos_server, '--config={}'.format(config_file),
               'start', '-t' '4', '--insecure',
               '--recreate-superblocks']

        my_env = os.environ.copy()
        my_env['CRT_PHY_ADDR_STR'] = 'ofi+sockets'
        my_env['OFI_INTERFACE'] = 'lo'
        my_env['D_LOG_MASK'] = 'INFO'
        my_env['DD_MASK'] = 'all'
        my_env['DAOS_DISABLE_REQ_FWD'] = '1'
        self._sp = subprocess.Popen(cmd, env=my_env)

        agent_file = os.path.expanduser(os.path.join('~', 'daos_agent.yml'))

        agent_bin = os.path.join(self._conf['PREFIX'], 'bin', 'daos_agent')

        print(agent_file)
        self._agent = subprocess.Popen([agent_bin,
                                        '--config-path={}'.format(agent_file),
                                        '-i'])
        time.sleep(2)

    def __del__(self):
        """Stop a previously started DAOS server"""
        if self._agent:
            self._agent.send_signal(signal.SIGINT)
            ret = self._agent.wait(timeout=5)
            print('rc from agent is {}'.format(ret))

        if not self._sp:
            return
        self._sp.send_signal(signal.SIGTERM)
        ret = self._sp.wait(timeout=5)
        print('rc from server is {}'.format(ret))
        #log_test(self._conf, '/tmp/server.log')

def il_cmd(dfuse, cmd):
    my_env = os.environ.copy()
    my_env['CRT_PHY_ADDR_STR'] = 'ofi+sockets'
    my_env['OFI_INTERFACE'] = 'lo'
    log_file = tempfile.NamedTemporaryFile(prefix='dfuse_il_', suffix='.log', delete=False)
    symlink_file('/tmp/dfuse_il_latest.log', log_file.name)
    my_env['D_LOG_FILE'] = log_file.name
    my_env['LD_PRELOAD'] = os.path.join(dfuse._conf['PREFIX'], 'lib64', 'libioil.so')
    my_env['D_LOG_MASK'] = 'DEBUG'
    my_env['DD_MASK'] = 'all'
    ret = subprocess.run(cmd, env=my_env)
    print('Logged il to {}'.format(log_file.name))
    print(ret)
    print('Log results for il')
    try:
        log_test(dfuse._conf, log_file.name)
    except:
        pass
    return ret

def symlink_file(a, b):
    if os.path.exists(a):
        os.remove(a)
    os.symlink(b, a)

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

        symlink_file('/tmp/dfuse_latest.log', self.log_file)

        if not os.path.exists(self.dir):
            os.mkdir(self.dir)

    def start(self):
        """Start a dfuse instance"""
        dfuse_bin = os.path.join(self._conf['PREFIX'], 'bin', 'dfuse')
        my_env = os.environ.copy()

        single_threaded = False
        caching = False

        pre_inode = os.stat(self.dir).st_ino

        my_env['CRT_PHY_ADDR_STR'] = 'ofi+sockets'
        my_env['OFI_INTERFACE'] = 'eth0'
        my_env['D_LOG_MASK'] = 'INFO,dfuse=DEBUG,dfs=DEBUG'
        my_env['DD_MASK'] = 'all'
        my_env['DD_SUBSYS'] = 'all'
        my_env['D_LOG_FILE'] = self.log_file

        cmd = ['valgrind', '--quiet']

        if True:
            cmd.extend(['--leak-check=full', '--show-leak-kinds=all'])

        if True:
            cmd.extend(['--suppressions={}'.format(os.path.join('src',
                                                                'cart',
                                                                'utils',
                                                                'memcheck-cart.supp')),
                        '--suppressions={}'.format(os.path.join('utils',
                                                                'memcheck-daos-client.supp'))])

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
                raise Exception('dfuse died waiting for start')
            except subprocess.TimeoutExpired:
                pass
            total_time += 1
            if total_time > 10:
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
        """Stop a previously started dfuse instance"""
        if not self._sp:
            return

        print('Stopping fuse')
        ret = umount(self.dir)
        if ret:
            self._close_files()
            umount(self.dir)

        ret = self._sp.wait(timeout=20)
        print('rc from dfuse {}'.format(ret))
        self._sp = None
        log_test(self._conf, self.log_file)


    def wait_for_exit(self):

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
    if sys.version_info.major < 3:
        pydir = 'python{}.{}'.format(sys.version_info.major, sys.version_info.minor)
    else:
        pydir = 'python{}'.format(sys.version_info.major)

    sys.path.append(os.path.join(conf['PREFIX'],
                                 'lib64',
                                 pydir,
                                 'site-packages'))

    os.environ['CRT_PHY_ADDR_STR'] = 'ofi+sockets'
    os.environ['OFI_INTERFACE'] = 'lo'

    import pydaos
    daos = __import__('pydaos')
    return daos

def show_cont(conf, pool):

    daos_bin = os.path.join(conf['PREFIX'], 'bin', 'daos')
    cmd = [daos_bin, 'container', 'create', '--svc', '0', '--pool', pool]
    rc = subprocess.run(cmd, capture_output=True)
    print('rc is {}'.format(rc))

    cmd = [daos_bin, 'pool', 'list-containers', '--svc', '0', '--pool', pool]
    rc = subprocess.run(cmd, capture_output=True)
    print('rc is {}'.format(rc))
    return rc.stdout.strip()

def make_pool(daos, conf):

    time.sleep(2)

    daos_raw = __import__('pydaos.raw')

    context = daos.raw.DaosContext(os.path.join(conf['PREFIX'], 'lib64'))

    pool_con = daos.raw.DaosPool(context)

    createuid = os.geteuid()
    creategid = os.getegid()

    try:
        pool_con.create(511, os.geteuid(), os.getegid(), 1024*1014*128, b'daos_server')
    except pydaos.raw.daos_api.DaosApiError:
        time.sleep(10)
        pool_con.create(511, os.geteuid(), os.getegid(), 1024*1014*128, b'daos_server')

    return get_pool_list()

D = None

def inspect_daos(conf, d):

    global D

    if sys.version_info.major < 3:
        pydir = 'python{}.{}'.format(sys.version_info.major, sys.version_info.minor)
    else:
        pydir = 'python{}'.format(sys.version_info.major)

    sys.path.append(os.path.join(conf['PREFIX'],
                                 'lib64',
                                 pydir,
                                 'site-packages'))

    os.environ['CRT_PHY_ADDR_STR'] = 'ofi+sockets'
    os.environ['OFI_INTERFACE'] = 'lo'

    import pydaos
    daos = __import__('pydaos')
    D = daos
    return inspect_daos2(conf, daos, d)

def inspect_daos2(conf, daos, d):

    PUID = '5b7867f5-653b-41ba-beae-18d465af2c25'
    CUID = 'b6ac4eb5-0db7-48d8-bd2b-b8bbde3461be'
    pools = get_pool_list()
    if pools:
        PUID=pools[0]

    if True:
        daos_raw = __import__('pydaos.raw')

        context = daos.raw.DaosContext(os.path.join(conf['PREFIX'], 'lib64'))

        pool_con = daos.raw.DaosPool(context)

        new_pool = False
        if PUID in pools:
            pool = PUID
            pool_con.set_uuid_str(pool)
            pool_con.set_svc(0)
            pool_con.set_group(b'daos_server')
            new_pool = True
        else:

            createuid = os.geteuid()
            creategid = os.getegid()

            pool_con.set_uuid_str(PUID)
            pool_con.create(511, os.geteuid(), os.getegid(), 1024*1014*128, b'daos_server')
            print(pool_con)
            PUID = pool_con.get_uuid_str()

        pool_con.connect(1<<1)
        cont_con = daos.raw.DaosContainer(context, poh=pool_con.handle, cuuid=CUID)
        cuid_t = uuid.UUID(CUID)
        if new_pool:
            cuuid = CUID
        else:

            cont_con.create(pool_con.handle, con_uuid=cuid_t)
            cuuid = cont_con.get_uuid_str()
            print('Created container', cuuid)

        dc = daos.Cont(pool_con.get_uuid_str(), cuuid)

    pools = get_pool_names(d, conf)
    dc = daos.Cont(pools[0], CUID)

    return (PUID, CUID)

    return

    print("Getting root kv")
    kv = dc.rootkv()

    print("Getting key")

    kv.put(b'house', b'street')
#    kv.put(1,2)
#    kv.put(b'house', 1)
#    kv.put(1, b'house')
    kv.put('band', 'madness')
    v = kv.get('house')
    print(v)
    print(dir(kv))
    for (k) in kv:
        print('{} : {}'.format(k,kv[k]))
    print(daos)

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

def run_cmd(pool, cont):

    s_dir = '/tmp/r'
    s_file = 'one'

    sd = shelve.open(os.path.join(s_dir, s_file), 'c')

    sd['here'] = 'close'
    sd['__length'] = 2
    sd[str(0)] = 'bob'
    sd[str(1)] = 'bob'
    sd[str(2)] = 'bob'
    sd.close()

    subprocess.run(['./src/client/pydaos/daosdbm.py',
                    pool,
                    cont,
                    s_dir,
                    s_file])


EFILES=['src/common/misc.c',
        'src/common/prop.c',
        'src/cart/crt_hg_proc.c',
        'src/security/cli_security.c',
        'src/client/dfuse/dfuse_core.c']

def log_test(conf, filename):

    file_self = os.path.dirname(os.path.abspath(__file__))
    logparse_dir = os.path.join(os.path.dirname(file_self), '../../../src/cart/test/util')
    crt_mod_dir = os.path.realpath(logparse_dir)
    print('cart dir is', crt_mod_dir)
    if crt_mod_dir not in sys.path:
        sys.path.append(crt_mod_dir)

    lp = __import__('cart_logparse')
    lt = __import__('cart_logtest')

    lt.shown_logs = set()

    lt.mismatch_alloc_ok['crt_proc_d_rank_list_t'] = ('rank_list',
                                                      'rank_list->rl_ranks')
    lt.mismatch_alloc_ok['path_gen'] = ('*fpath')
    lt.mismatch_alloc_ok['get_attach_info'] = ('reqb')
    lt.mismatch_alloc_ok['iod_fetch'] = ('biovs')
    lt.mismatch_alloc_ok['bio_sgl_init'] = ('sgl->bs_iovs')
    lt.mismatch_alloc_ok['process_credential_response'] = ('bytes')
    lt.mismatch_alloc_ok['pool_map_find_tgts'] = ('*tgt_pp')
    lt.mismatch_alloc_ok['daos_acl_dup'] = ('acl_copy')
    lt.mismatch_alloc_ok['dfuse_pool_lookup'] = ('ie', 'dfs', 'dfp')
    lt.mismatch_alloc_ok['pool_prop_read'] = ('prop->dpp_entries[idx].dpe_str',
                                              'prop->dpp_entries[idx].dpe_val_ptr')
    lt.mismatch_alloc_ok['cont_prop_read'] = ('prop->dpp_entries[idx].dpe_str')
    lt.mismatch_alloc_ok['cont_iv_prop_g2l'] = ('prop_entry->dpe_str')
    lt.mismatch_alloc_ok['notify_ready'] = ('reqb')
    lt.mismatch_alloc_ok['pack_daos_response'] = ('body')
    lt.mismatch_alloc_ok['ds_mgmt_drpc_get_attach_info'] = ('body')
    lt.mismatch_alloc_ok['pool_prop_default_copy'] = ('entry_def->dpe_str')
    lt.mismatch_alloc_ok['daos_prop_dup'] = ('entry_dup->dpe_str')
    lt.mismatch_alloc_ok['auth_cred_to_iov'] = ('packed')

    lt.mismatch_free_ok['d_rank_list_free'] = ('rank_list',
                                               'rank_list->rl_ranks')
    lt.mismatch_free_ok['pool_prop_default_copy'] = ('entry_def->dpe_str')
    lt.mismatch_free_ok['pool_svc_store_uuid_cb'] = ('path')
    lt.mismatch_free_ok['ds_mgmt_svc_start'] = ('uri')
    lt.mismatch_free_ok['daos_acl_free'] = ('acl')
    lt.mismatch_free_ok['drpc_free'] = ('pointer')
    lt.mismatch_free_ok['pool_child_add_one'] = ('path')
    lt.mismatch_free_ok['get_tgt_rank'] = ('tgts')
    lt.mismatch_free_ok['bio_sgl_fini'] = ('sgl->bs_iovs')
    lt.mismatch_free_ok['daos_iov_free'] = ('iov->iov_buf'),
    lt.mismatch_free_ok['daos_prop_free'] = ('entry->dpe_str',
                                             'entry->dpe_val_ptr')
    lt.mismatch_free_ok['main'] = ('dfs')
    lt.mismatch_free_ok['start_one'] = ('path')
    lt.mismatch_free_ok['pool_svc_load_uuid_cb'] = ('path')
    lt.mismatch_free_ok['ie_sclose'] = ('ie', 'dfs', 'dfp')
    lt.mismatch_free_ok['notify_ready'] = ('req.uri')
    lt.mismatch_free_ok['get_tgt_rank'] = ('tgts')

    lt.memleak_ok.append('dfuse_start')
    lt.memleak_ok.append('expand_vector')
    lt.memleak_ok.append('d_rank_list_alloc')
    lt.memleak_ok.append('get_tpv')
    lt.memleak_ok.append('get_new_entry')
    lt.memleak_ok.append('get_attach_info')
    lt.memleak_ok.append('drpc_call_create')

    log_iter = lp.LogIter(filename)
    lto = lt.LogTest(log_iter)

    for efile in EFILES:
        lto.set_error_ok(efile)

    lto.check_log_file(abort_on_warning=True)

def create_and_read_via_il(dfuse, path):
    fname = os.path.join(path, 'test_file')
    ofd = open(fname, 'w')
    ofd.write('hello ')
    ofd.write('world\n')
    ofd.flush()
    assert_file_size(ofd, 12)
    print(os.fstat(ofd.fileno()))
    ofd.close()
    il_cmd(dfuse, ['cat', fname])

def run_dfuse(server, conf):
    """Run several dfuse instances"""

    daos = import_daos(server, conf)

    pools = get_pool_list()
    while len(pools) < 1:
        pools = make_pool(daos, conf)

    #dfuse = DFuse(server, conf, pool=pools[0])

    dfuse = DFuse(server, conf)
    try:
        pre_stat = os.stat(dfuse.dir)
    except OSError:
        umount(dfuse.dir)
        raise
    container = str(uuid.uuid4())
    dfuse.start()
    print(os.statvfs(dfuse.dir))
    subprocess.run(['df', '-h'])
    subprocess.run(['df', '-i', dfuse.dir])
#    return
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
    dfuse = None

    uns_container = container

    container2 = str(uuid.uuid4())
    dfuse = DFuse(server, conf, pool=pools[0])
    pre_stat = os.stat(dfuse.dir)
    dfuse.start()
    print('Running dfuse with pool only')
    stat_and_check(dfuse, pre_stat)
    check_no_file(dfuse)
    cpath = os.path.join(dfuse.dir, container2)
    os.mkdir(cpath)
    #cstat = os.stat(cpath)
    #print(cstat)
    cdir = os.path.join(dfuse.dir, container)
    #create_and_read_via_il(dfuse, cdir)

    dfuse = None

    dfuse = DFuse(server, conf, pool=pools[0], container=container)
    pre_stat = os.stat(dfuse.dir)
    dfuse.start()
    print('Running fuse with both')

#    try:
#        print('Waiting')
#        dfuse.wait_for_exit()
#    except KeyboardInterrupt:
#        pass

    stat_and_check(dfuse, pre_stat)

    create_and_read_via_il(dfuse, dfuse.dir)

    run_tests(dfuse)

#    uns_container = str(uuid.uuid4())
#    # Make a container for UNS to connect to.
#    os.mkdir(os.path.join(dfuse.dir, pools[0], uns_container))
    return

    daos_bin = os.path.join(conf['PREFIX'], 'bin', 'daos')

    dfuse = None

    dfuse = DFuse(server, conf, pool=pools[0], container=container2)
    dfuse.start()

    uns_path = os.path.join(dfuse.dir, 'ep0')

    cmd = [daos_bin, 'container', 'uns-insert', '--svc', '0',
           '--pool', pools[0], '--cont', uns_container,
           '--path', uns_path]

    print('Inserting entry point')
    rc = subprocess.run(cmd)
    print('rc is {}'.format(rc))
    print(os.stat(uns_path))
    print(os.stat(uns_path))
    print(os.listdir(dfuse.dir))

    dfuse = None



    print('Trying UNS')
    dfuse = DFuse(server, conf)
    dfuse.start()

    # List the root container.
    print(os.listdir(os.path.join(dfuse.dir, pools[0], container2)))

    uns_path = os.path.join(dfuse.dir, pools[0], container2, 'ep')
    direct_path = os.path.join(dfuse.dir, pools[0], uns_container)

    cmd = [daos_bin, 'container', 'uns-insert', '--svc', '0',
           '--pool', pools[0], '--cont', uns_container,
           '--path', uns_path]

    print('Inserting entry point')
    rc = subprocess.run(cmd)
    print('rc is {}'.format(rc))

    # List the root container again.
    print(os.listdir(os.path.join(dfuse.dir, pools[0], container2)))

    # List the target container.
    files = os.listdir(direct_path)
    print(files)
    # List the target container through UNS.
    print(os.listdir(uns_path))
    direct_stat = os.stat(os.path.join(direct_path, files[0]))
    uns_stat = os.stat(os.path.join(uns_path, files[0]))
    print(direct_stat)
    print(uns_stat)
    assert(uns_stat.st_ino == direct_stat.st_ino)

#    return

    dfuse = None
    print('Trying UNS with previous cont')
    dfuse = DFuse(server, conf)
    dfuse.start()

    files = os.listdir(direct_path)
    print(files)
    print(os.listdir(uns_path))

    direct_stat = os.stat(os.path.join(direct_path, files[0]))
    uns_stat = os.stat(os.path.join(uns_path, files[0]))
    print(direct_stat)
    print(uns_stat)
    assert(uns_stat.st_ino == direct_stat.st_ino)

    print('Reached the end, no errors')

def run_il_test(server, conf):

    daos = import_daos(server, conf)

    pools = get_pool_list()

    while len(pools) < 2:
        pools = make_pool(daos, conf)

    print('pools are ', ','.join(pools))

    containers = ['62176a51-8229-4e4c-ad1b-43aaace8a97a',
                  '4ef12a58-c544-406c-8acf-56a2c0589cd6']

    dfuse = DFuse(server, conf)
    dfuse.start()

    dirs = []

    for p in pools:
        for c in containers:
            d = os.path.join(dfuse.dir, p, p)
            try:
                print('Making directory {}'.format(d))
                os.mkdir(d)
            except FileExistsError:
                pass
            dirs.append(d)

    f = os.path.join(dirs[0], 'file')
    fd = open(f, 'w')
    fd.write('Hello')
    fd.close
    il_cmd(dfuse, ['cp', f, dirs[-1]])
#    il_cmd(dfuse, ['cp', '/bin/bash', dirs[-1]])
#    il_cmd(dfuse, ['md5sum', os.path.join(dirs[-1], 'bash')])
    dfuse = None

def run_in_fg(server, conf):

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
    print('daos container create --svc 0 --type POSIX --pool {} --path {}/uns-link'.format(
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

    daos = import_daos(server, conf)

    dbm = __import__('daosdbm')

    pools = get_pool_list()

    while len(pools) < 1:
        pools = make_pool(daos, conf)

    pool = pools[0]
        
    container = show_cont(conf, pool)

    print(container)
    print(container.decode())
    kvg = dbm.daos_named_kv('ofi+sockets', 'lo', pool, container.decode())

    kv = kvg.get_kv_by_name('Dave')
    kv['a'] = 'a'
    kv['b'] = 'b'
    kv['list'] = pickle.dumps(list(range(1,100000)))
    for k in range(1,100):
        kv[str(k)] = pickle.dumps(list(range(1,10)))
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

#    kv.bget(data, value_size=368870);
    kv.bget(data, value_size=16);
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

def main():
    """Main entry point"""

    conf = load_conf()
    server = DaosServer(conf)
    server.start()

    if len(sys.argv) == 2 and sys.argv[1] == 'launch':
        run_in_fg(server, conf)
    elif len(sys.argv) == 2 and sys.argv[1] == 'kv':
        test_pydaos_kv(server, conf)
    else:
        #run_il_test(server, conf)
        #(pool, cont) = inspect_daos(conf, daos)
        run_dfuse(server, conf)

        #(pool, cont) = inspect_daos(conf, daos)
        #print('{} {}'.format(pool, cont))
        #run_cmd(pool, cont)

if __name__ == '__main__':
    main()
