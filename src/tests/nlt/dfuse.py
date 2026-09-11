"""NLT: DFuse mount management and test decorators.

(C) Copyright 2020-2024 Intel Corporation.
(C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP
(C) Copyright 2025 Google LLC
(C) Copyright 2025 Enakta Labs Ltd

SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import functools
import os
import re
import signal
import subprocess  # nosec
import tempfile
import threading
import time
from os.path import join

from .base import NLTestFail, NLTestIlZeroCall, get_inc_id, set_active_test, umount
from .client import DaosCont, DaosPool, ValgrindHelper, run_daos_cmd
from .config import get_base_env
from .logging_utils import log_test


class DFuse():
    """Manage a dfuse instance"""

    instance_num = 0

    # pylint: disable-next=too-many-arguments
    def __init__(self, daos, conf, pool=None, container=None, mount_path=None, uns_path=None,
                 caching=True, wbcache=True, multi_user=False, ro=False, dump_h=False,
                 read_h=False, file_h=None):
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
            if self.pool and self.pool != container.pool.id():
                raise ValueError(
                    f'container.pool.id() = {container.pool.id()} but self.pool = {self.pool}')
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
        self._ro = ro
        self.dump_h = dump_h
        self.read_h = read_h
        self.file_h = file_h

        self.valgrind = None
        os.makedirs(self.dir, exist_ok=True)

    def __str__(self):

        if self._sp:
            running = 'running'
        else:
            running = 'not running'

        return f'DFuse instance at {self.dir} ({running})'

    def start(self, v_hint=None, use_oopt=False):
        """Start a dfuse instance"""
        # pylint: disable=too-many-branches
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

        if not self.cores:
            # Use a lower default thread-count for NLT due to running tests in parallel.
            cmd.extend(['--thread-count', '4'])

        if not self.caching:
            cmd.append('--disable-caching')
        else:
            if not self.wbcache:
                cmd.append('--disable-wb-cache')

        if self.dump_h:
            cmd.extend(['--dump-handles', self.file_h])
        if self.read_h:
            cmd.extend(['--read-handles', self.file_h])

        if self._ro:
            cmd.append('--read-only')

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
                # Kill the unresponsive dfuse command
                self._sp.send_signal(signal.SIGTERM)
                self._sp = None
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

        if self.container:
            # This queries the mount that may itself be wedged.
            self.run_query(use_json=True, timeout=120)
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
        if self.caching:
            check_fstat = False

        # DAOS-16585: Disable fstat checking for non Red Hat systems which appear to use a different
        # implementation of fstat which isn't yet intercepted.  This allows testing to progress in
        # the absence of this feature.
        if not os.path.exists("/etc/redhat-release"):
            check_fstat = False

        my_env = get_base_env()
        prefix = f'dnt_ioil_{cmd[0]}_{get_inc_id()}_'
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

        try:
            log_test(self.conf, log_name, check_read=check_read, check_write=check_write,
                     check_fstat=check_fstat)
        except NLTestIlZeroCall as error:
            error.command = cmd
            raise

        assert ret.returncode == 0, ret
        return ret

    def run_query(self, use_json=False, quiet=False, timeout=None):
        """Run filesystem query"""
        rc = run_daos_cmd(self.conf, ['filesystem', 'query', self.dir],
                          use_json=use_json, log_check=quiet, valgrind=quiet, timeout=timeout)
        print(rc)
        return rc

    def check_usage(self, ino=None, inodes=None, open_files=None, pools=None, containers=None,
                    qpath=None):
        """Query and verify the dfuse statistics.

        Returns the raw numbers in a dict.
        """
        cmd = ['filesystem', 'query', qpath or self.dir]

        if ino is not None:
            cmd.extend(['--inode', str(ino)])
        rc = run_daos_cmd(self.conf, cmd, use_json=True)
        print(rc)
        assert rc.returncode == 0, rc

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

    def evict_and_wait(self, paths, qpath=None):
        """Evict a number of paths from dfuse"""
        inodes = []
        for path in paths:
            rc = self._evict_path(path)
            inodes.append(rc['inode'])

        sleeps = 0

        for inode in inodes:
            found = True
            while found:
                rc = self.check_usage(inode, qpath=qpath)
                print(rc)
                found = rc['resident']
                if not found:
                    sleeps += 1
                    assert sleeps < 10, 'Path still present 10 seconds after eviction'
                    time.sleep(1)


def needs_dfuse(method):
    """Decorator function for starting dfuse under posix_tests class

    Runs every test twice, once with caching enabled, and once with
    caching disabled.
    """
    @functools.wraps(method)
    def _helper(self):
        # filter out anything we were told not to run
        filtered_caching_variants = \
            [x for x in [False, True] if x not in
             needs_dfuse_with_opt.get_excluded_versions(method.__name__)]
        caching = filtered_caching_variants[self.call_index]
        self.needs_more = len(filtered_caching_variants) > self.call_index + 1
        self.test_name = needs_dfuse_with_opt.parameterized_test_to_name(method.__name__, caching)
        # Attribute findings to the reported (parameterized) test name, not the base method.
        set_active_test(self.test_name)

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
    needs_dfuse_with_opt.record_wrap(method.__name__, [False, True])
    return _helper


# pylint: disable-next=invalid-name
class needs_dfuse_with_opt():
    """Decorator class for starting dfuse under posix_tests class

    By default runs the method twice, once with caching and once without, however can be
    configured to behave differently.  Interacts with the run_posix_tests._run_test() method
    to achieve this.
    """

    # dict of names that have been decorated either by needs_dfuse_with_opt or needs_dfuse
    # values are list of possible variants
    wrapped_names = {}

    # dict of tests that have been excluded at runtime (so we know what to skip)
    # values are the versions of the test to skip
    excluded_name_dict = {}

    # ensure thread safety; not sure this is actually necessary, but meh...
    wrapping_lock = threading.Lock()

    # pylint: disable=too-few-public-methods
    def __init__(self, caching_variants=None, wbcache=True, dfuse_inval=True, ro=False):
        self.caching_variants = caching_variants if caching_variants else [False, True]
        self.wbcache = wbcache
        self.dfuse_inval = dfuse_inval
        self.ro = ro

    def __call__(self, method):
        """Wrapper function"""
        @functools.wraps(method)
        def _helper(obj):

            args = {"container": obj.container}

            # filter out anything we were told not to run
            filtered_caching_variants = \
                [x for x in self.caching_variants if x not in
                 needs_dfuse_with_opt.get_excluded_versions(method.__name__)]
            caching = filtered_caching_variants[obj.call_index]
            obj.needs_more = len(filtered_caching_variants) > obj.call_index + 1
            obj.test_name = \
                needs_dfuse_with_opt.parameterized_test_to_name(method.__name__, caching)
            # Attribute findings to the reported (parameterized) test name, not the base method.
            set_active_test(obj.test_name)

            if not self.dfuse_inval:
                assert caching is True
                cont_attrs = {'dfuse-attr-time': '5m',
                              'dfuse-dentry-time': '5m',
                              'dfuse-dentry-dir-time': '5m',
                              'dfuse-ndentry-time': '5m'}
                obj.container.set_attrs(cont_attrs)
            elif caching:
                cont_attrs = {'dfuse-attr-time': '1m',
                              'dfuse-dentry-time': '1m',
                              'dfuse-dentry-dir-time': '1m',
                              'dfuse-ndentry-time': '1m'}
                obj.container.set_attrs(cont_attrs)

            if self.ro:
                args["ro"] = True

            obj.dfuse = DFuse(obj.server,
                              obj.conf,
                              caching=caching,
                              wbcache=self.wbcache,
                              **args)
            obj.dfuse.start(v_hint=method.__name__)
            try:
                rc = method(obj)
            finally:
                if obj.dfuse.stop():
                    obj.fatal_errors = True
            return rc

        needs_dfuse_with_opt.record_wrap(method.__name__, self.caching_variants)
        return _helper

    @staticmethod
    def get_excluded_versions(name):
        """Get the excluded variants of a test"""
        with needs_dfuse_with_opt.wrapping_lock:
            return list(needs_dfuse_with_opt.excluded_name_dict[name]) \
                if name in needs_dfuse_with_opt.excluded_name_dict else []

    @staticmethod
    def record_wrap(name, caching_variants):
        """Record that a test is being wrapped and which variants of the test are wrapped"""
        needs_dfuse_with_opt.wrapped_names[name] = list(caching_variants)

    @staticmethod
    def record_exclusions(excluded_name_dict):
        """Note at runtime which variants of a test are being excluded"""
        needs_dfuse_with_opt.excluded_name_dict = excluded_name_dict

    @staticmethod
    def parameterized_test_to_name(name, caching):
        """Convert a parametrization to a string name for a test"""
        suffix = 'caching_on' if caching else 'caching_off'
        return name + '_' + suffix

    @staticmethod
    def get_test_variants(name):
        """Return which variants of a test have been wrapped"""
        if name not in needs_dfuse_with_opt.wrapped_names:
            return []

        return list(needs_dfuse_with_opt.wrapped_names[name])

    @staticmethod
    def parse_test_name(name):
        """Convert a string name for a parameterized test to a parameterized tuple"""
        match = re.match(r'(.+)_(caching_on|caching_off)$', name)
        if not match:
            return (name, None)
        return (match.group(1), match.group(2) == 'caching_on')
