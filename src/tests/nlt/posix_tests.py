"""NLT: POSIX/DFuse functional tests.

(C) Copyright 2020-2024 Intel Corporation.
(C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP
(C) Copyright 2025 Google LLC
(C) Copyright 2025 Enakta Labs Ltd

SPDX-License-Identifier: BSD-2-Clause-Patent
"""

# pylint: disable=too-many-lines

import errno
import importlib
import os
import pwd
import random
import shutil
import stat
import subprocess  # nosec
import sys
import tempfile
import threading
import time
import traceback
from os.path import join

import tabulate
import xattr

from .base import NLTestFail, set_active_test
from .client import (check_dir_attr, check_dir_pl_attr, check_file_attr, check_file_pl_attr,
                     create_cont, destroy_container, run_daos_cmd, run_fs_get_attr)
from .config import get_base_env
from .dfuse import DFuse, needs_dfuse, needs_dfuse_with_opt
from .helpers import create_and_read_via_il, run_container_query
from .logging_utils import NltStderrWrapper, NltStdoutWrapper


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

    @staticmethod
    def generate_test_list():
        """Generate list of Posix tests"""
        return [x for x in dir(PosixTests) if x.startswith('test')]

    @staticmethod
    def generate_manual_test_list():
        """Generate list of opt-in manual tests (too slow/expensive for CI)"""
        return [x for x in dir(PosixTests) if x.startswith('manual_')]

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

    @needs_dfuse_with_opt(caching_variants=[False])
    def test_oclass(self):
        """Test container object class options"""

        container = create_cont(self.conf, self.pool, ctype="POSIX", label='oclass_test')
        rc = run_daos_cmd(self.conf,
                          ['container', 'query',
                           self.pool.id(), container.id()],
                          show_stdout=True, use_json=True)
        print(rc)
        assert rc.returncode == 0
        assert rc.json['response'].get('dir_object_class') not in (None, 'UNKNOWN')
        assert rc.json['response'].get('file_object_class') not in (None, 'UNKNOWN')
        container.destroy()

        container = create_cont(self.conf, self.pool, ctype="POSIX", label='oclass_test',
                                oclass='S1', dir_oclass='S2', file_oclass='S4')
        rc = run_daos_cmd(self.conf,
                          ['container', 'query',
                           self.pool.id(), container.id()],
                          show_stdout=True, use_json=True)
        print(rc)
        assert rc.returncode == 0
        assert rc.json['response']['object_class'] == 'S1'

        dfuse = DFuse(self.server, self.conf, container=container)
        dfuse.use_valgrind = False
        dfuse.start()

        dir1 = join(dfuse.dir, 'd1')
        os.mkdir(dir1)
        file1 = join(dir1, 'f1')
        with open(file1, 'w') as ofd:
            ofd.write('hello')

        data = run_fs_get_attr(self.conf, '--path', dir1)
        assert check_dir_attr(data, 'S2', 'S4', 'S2', 1048576)

        data = run_fs_get_attr(self.conf, '--path', file1)
        assert check_file_attr(data, 'S4', 1048576)

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

    def test_pre_read(self):
        """Test the pre-read code.

        Test reading a file which is previously unknown to fuse with caching on.  This should go
        into the pre_read code and load the file contents automatically after the open call.
        """
        dfuse = DFuse(self.server, self.conf, container=self.container)
        dfuse.start(v_hint='pre_read_0')

        with open(join(dfuse.dir, 'file0'), 'w') as fd:
            fd.write('test')

        with open(join(dfuse.dir, 'file1'), 'w') as fd:
            fd.write('test')

        with open(join(dfuse.dir, 'file2'), 'w') as fd:
            fd.write('testing')

        raw_data0 = ''.join(random.choices(['d', 'a', 'o', 's'], k=1024 * 1024))  # nosec
        with open(join(dfuse.dir, 'file3'), 'w') as fd:
            fd.write(raw_data0)

        raw_data1 = ''.join(random.choices(['d', 'a', 'o', 's'], k=(1024 * 1024) - 1))  # nosec
        with open(join(dfuse.dir, 'file4'), 'w') as fd:
            fd.write(raw_data1)

        if dfuse.stop():
            self.fatal_errors = True

        dfuse = DFuse(self.server, self.conf, caching=True, container=self.container)
        dfuse.start(v_hint='pre_read_1')

        with open(join(dfuse.dir, 'file0'), 'r') as fd:
            data0 = fd.read()

        with open(join(dfuse.dir, 'file1'), 'r') as fd:
            data1 = fd.read(16)

        with open(join(dfuse.dir, 'file2'), 'r') as fd:
            data2 = fd.read(2)

        with open(join(dfuse.dir, 'file3'), 'r') as fd:
            data3 = fd.read()

        with open(join(dfuse.dir, 'file4'), 'r') as fd:
            data4 = fd.read()
            data5 = fd.read()

        # This should not use the pre-read feature, to be validated via the logs.
        with open(join(dfuse.dir, 'file4'), 'r') as fd:
            data6 = fd.read()

        if dfuse.stop():
            self.fatal_errors = True
        print(data0)
        assert data0 == 'test'
        assert data1 == 'test'
        assert data2 == 'te'
        assert raw_data0 == data3
        assert raw_data1 == data4
        assert len(data5) == 0
        assert raw_data1 == data6

    def test_two_mounts(self):
        """Create two mounts, and check that a file created in one can be read from the other"""

        try:
            fd, tmpfile = tempfile.mkstemp(prefix="my_temp_file_", dir="/tmp")
            print(f"Created temp file: {tmpfile}")
            os.close(fd)

        except OSError as e:
            print(f"mkstemp failed: {e}", file=sys.stderr)
            sys.exit(1)

        dfuse0 = DFuse(self.server,
                       self.conf,
                       caching=False,
                       dump_h=True,
                       file_h=tmpfile,
                       container=self.container)
        dfuse0.start(v_hint='two_0')

        dfuse1 = DFuse(self.server,
                       self.conf,
                       caching=True,
                       read_h=True,
                       file_h=tmpfile,
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

        if dfuse1.stop():
            self.fatal_errors = True
        if dfuse0.stop():
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

        self.dfuse.il_cmd([
            'dd',
            f'if={fname}',
            'of=/dev/null',
            'bs=4096',
            'iflag=fullblock',
            'status=none'
        ], check_write=False, check_fstat=False)

    @needs_dfuse_with_opt(caching_variants=[False])
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
        dst = join(sub_cont_dir, 'file')
        self.dfuse.il_cmd([
            'dd',
            f'if={file}',
            f'of={dst}',
            'bs=4096',
            'iflag=fullblock',
            'status=none'
        ], check_fstat=False)

        # Copy it within the container.
        child_dir = join(self.dfuse.dir, 'new_dir')
        os.mkdir(child_dir)
        dst = join(child_dir, 'file')

        self.dfuse.il_cmd([
            'dd',
            f'if={file}',
            f'of={dst}',
            'bs=128K',
            'status=none'
        ], check_fstat=False)

        # Copy something into a container
        dst = join(sub_cont_dir, 'bash')

        self.dfuse.il_cmd([
            'dd',
            'if=/bin/bash',
            f'of={dst}',
            'bs=128K',
            'status=none'
        ], check_read=False, check_fstat=False)

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

    @needs_dfuse_with_opt(caching_variants=[False])
    def test_stable_inode(self):
        """Ensure that container inodes are persistent

        Create a container via dfuse, access it to query the inode, evict the inode and then access
        it again forcing a re-connect and verify the inode is unchanged.
        """
        child_path = join(self.dfuse.dir, 'test_cont')

        new_cont = create_cont(self.conf, self.pool, path=child_path)

        pre = os.stat(child_path)

        self.dfuse.evict_and_wait([child_path])

        post = os.stat(child_path)

        # Close and detach again.
        self.dfuse.evict_and_wait([child_path])
        new_cont.destroy()

        print(pre)
        print(post)
        assert pre.st_ino == post.st_ino

    def manual_stable_cont_inode(self):
        """Ensure that container inodes are persistent

        Create a container outside of dfuse.
        Start dfuse with no container on command line.
        Access pool path and read ino
        Wait for pool path to be evicted
        Access container path read ino
        Wait for pool path to be evicted
        Access container path check ino

        Note: Test passes but is disabled due to run-time.
        """

        # Magic value for how long to sleep.  This needs to be long enough for entry timeout,
        # whatever grace period is configured and some additional to let the eviction process
        # happen.  This makes for a very long test, plus in addition there is no way of telling
        # if the eviction has actually happened.
        # A second test pool would be useful here so we could use filesystem query to check the
        # inode count.
        sleep_time = 307 + 5 + (60 * 30)

        cont0 = create_cont(self.conf, self.pool, label="stable1", ctype="POSIX")
        cont1 = create_cont(self.conf, self.pool, label="stable2", ctype="POSIX")

        dfuse = DFuse(self.server, self.conf)
        dfuse.start()

        root_data = os.stat(dfuse.dir)
        print(root_data)

        pool_data = os.stat(join(dfuse.dir, self.pool.uuid))
        print(pool_data)

        time.sleep(sleep_time)

        pool_data_post = os.stat(join(dfuse.dir, self.pool.uuid))
        print(pool_data_post)

        time.sleep(sleep_time)

        cont_data = os.stat(join(dfuse.dir, self.pool.uuid, cont0.uuid))
        print(cont_data)

        time.sleep(sleep_time)

        cont_data_post = os.stat(join(dfuse.dir, self.pool.uuid, cont0.uuid))
        print(cont_data_post)

        cont1_data = os.stat(join(dfuse.dir, self.pool.uuid, cont1.uuid))
        print(cont1_data)

        if dfuse.stop():
            self.fatal_errors = True

        cont0.destroy()
        assert root_data.st_ino == 1
        assert pool_data.st_ino == 2
        assert pool_data_post.st_ino == pool_data.st_ino
        assert cont_data.st_ino == 3
        assert cont_data_post.st_ino == cont_data.st_ino
        assert cont1_data.st_ino == 4

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

    @needs_dfuse_with_opt(wbcache=True, caching_variants=[True])
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
        rc = self.dfuse.run_query()
        assert rc.returncode == 0
        rc = self.dfuse.run_query(use_json=True)
        assert rc.returncode == 0

    @needs_dfuse_with_opt(dfuse_inval=False, caching_variants=[True])
    def test_uns_link(self):
        """Test to create a container then create a path for it in dfuse.

        Runs with dfuse already started, creates two new containers without links.

        Links one container into UNS and then destroys it through the link.

        Links the second container into UNS and then destroys it through the link, but checking
        the inode counts before and after.

        This test requires caching attributes to be set on the second container so that it does
        not get evicted before the inode count check.
        """
        # Create a new container which not linked
        container1 = create_cont(self.conf, self.pool, ctype="POSIX", label='mycont_uns_link1')
        cmd = ['cont', 'query', self.pool.id(), container1.id()]
        rc = run_daos_cmd(self.conf, cmd)
        assert rc.returncode == 0

        # Create a second new container which is not linked
        cont_attrs = {'dfuse-attr-time': '5m',
                      'dfuse-dentry-time': '5m',
                      'dfuse-dentry-dir-time': '5m',
                      'dfuse-ndentry-time': '5m'}
        container2 = create_cont(self.conf, self.pool, ctype="POSIX", label='mycont_uns_link2',
                                 attrs=cont_attrs)

        # Link and then destroy the first container
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
        assert rc.returncode == 0

        # Link and then destroy the second container but check inode count before and after
        # destroying.
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

    def test_uns_broken(self):
        """Test the behavior of a broken UNS link"""

        dfuse = DFuse(self.server, self.conf, container=self.container, caching=False)
        dfuse.start('uns-broken')

        i_path = join(dfuse.dir, "top_dir")

        os.mkdir(i_path)

        cont_path = join(i_path, "sub_cont")

        container = create_cont(self.conf, self.pool, ctype="POSIX", label="uns_broken",
                                path=cont_path)

        stat_pre = os.stat(cont_path)

        dfuse.evict_and_wait([cont_path])

        stat_post = os.stat(cont_path)
        assert stat_pre.st_ino == stat_post.st_ino

        dfuse.evict_and_wait([cont_path])

        container.destroy(valgrind=False, log_check=False)

        try:
            os.stat(cont_path)
            assert False
        except OSError as error:
            assert error.errno == errno.ENOLINK

        # Now check this readdir works.
        dfuse.evict_and_wait([i_path])
        files = os.listdir(i_path)
        print(files)
        assert files == ["sub_cont"]

        # Now evict again and check stat once readdir has put the inode in memory.
        dfuse.evict_and_wait([i_path])
        try:
            os.stat(cont_path)
            assert False
        except OSError as error:
            assert error.errno == errno.ENOLINK

        dfuse.evict_and_wait([i_path])

        if dfuse.stop():
            self.fatal_errors = True

    def test_uns_broken_ic(self):
        """Test the behavior of a broken UNS link when the link is in cache

        This test will create EINVAL errors from dfuse so silence them in the log checking.
        """
        dfuse = DFuse(self.server, self.conf, container=self.container, caching=False)
        dfuse.start('uns-broken-1')

        i_path = join(dfuse.dir, "top_dir")

        os.mkdir(i_path)

        cont_path = join(i_path, "sub_cont")

        container = create_cont(self.conf, self.pool, ctype="POSIX", label="uns_broken_ic",
                                path=cont_path)

        os.stat(cont_path)

        container.destroy(valgrind=False, log_check=False, force=True)

        try:
            os.stat(cont_path)
            assert False
        except OSError as error:
            assert error.errno == errno.ENOLINK

        dfuse.evict_and_wait([i_path])
        if dfuse.stop(ignore_einval=True):
            self.fatal_errors = True

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

        # Rename file 0 to file 1 in the background, this will remove the old file 1
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

    @needs_dfuse_with_opt(caching_variants=[False])
    def test_create_exists(self):
        """Test creating a file.

        This tests for create where the dentry being created already exists and is a file that's
        known to dfuse.

        To do this make a file in dfuse, use a back channel to rename it and then create a file
        using the new name."""

        filename = join(self.dfuse.dir, 'myfile')

        with open(filename, 'w') as fd:
            fd.write('hello')

        filename = join(self.dfuse.dir, 'newfile')
        try:
            os.stat(filename)
            raise NLTestFail("File exists")
        except FileNotFoundError:
            pass

        # Start another dfuse instance to move the files around without the kernel knowing.
        dfuse = DFuse(self.server,
                      self.conf,
                      container=self.container,
                      caching=False)
        dfuse.start(v_hint='create_exists_1')

        os.rename(join(dfuse.dir, 'myfile'), join(dfuse.dir, 'newfile'))

        filename = join(self.dfuse.dir, 'newfile')

        with open(filename, 'w') as fd:
            fd.write('hello')

        if dfuse.stop():
            self.fatal_errors = True

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

    def test_cont_chown(self):
        """Test ownership change of a POSIX container"""
        # Update container ACLs so current user can mount.
        rc = run_daos_cmd(self.conf, ['container',
                                      'update-acl',
                                      self.pool.id(),
                                      self.container.id(),
                                      '--entry',
                                      f'A::{os.getlogin()}@:rwta'])
        print(rc)
        assert rc.returncode == 0

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
        assert rc.returncode == 0

        # get owner to verify.
        rc = run_daos_cmd(self.conf, ['container',
                                      'get-acl',
                                      self.pool.id(),
                                      self.container.id()],
                          use_json=True)
        print(rc)
        assert rc.returncode == 0
        data = rc.json
        assert data['status'] == 0, rc
        assert data['error'] is None, rc
        assert data['response']['owner_user'] == 'root@'
        assert data['response']['owner_group'] == 'root@'

        dfuse = DFuse(self.server,
                      self.conf,
                      container=self.container,
                      caching=False)

        dfuse.start(v_hint='cont_chown_1')
        assert pwd.getpwnam('root').pw_uid == os.stat(dfuse.dir).st_uid
        assert pwd.getpwnam('root').pw_gid == os.stat(dfuse.dir).st_gid

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

        cont_attrs = {'dfuse-attr-time': '5m',
                      'dfuse-dentry-time': '5m',
                      'dfuse-dentry-dir-time': '5m',
                      'dfuse-ndentry-time': '5m'}
        container.set_attrs(cont_attrs)

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

        uns_container.set_attrs(cont_attrs)

        print('Trying UNS')
        dfuse = DFuse(server, conf, caching=True)
        dfuse.start('uns-1')

        # List the root container.
        print(os.listdir(join(dfuse.dir, pool, container.uuid)))

        # Now create a UNS link from the 2nd container to a 3rd one.
        uns_path = join(dfuse.dir, pool, container.uuid, 'ep0', 'ep')
        second_path = join(dfuse.dir, pool, uns_container.uuid)

        # Make a link within the new container.
        print('Inserting entry point')
        uns_container_2 = create_cont(conf, pool=self.pool, path=uns_path)

        uns_container_2.set_attrs(cont_attrs)
        dfuse.evict_and_wait([uns_path], qpath=join(dfuse.dir, pool, container.uuid))

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
        dfuse = DFuse(server, conf, caching=True)
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

    @needs_dfuse_with_opt(caching_variants=[False])
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
        data = run_fs_get_attr(self.conf, '--path', dir1)
        assert check_dir_attr(data, 'S1', None, 'S1', 1048576)

        # run same command using pool, container, dfs-path, and dfs-prefix
        data = run_fs_get_attr(self.conf, pool, uns_container.id(),
                               '--dfs-path', dir1, '--dfs-prefix', uns_path)
        assert check_dir_attr(data, 'S1', None, 'S1', 1048576)

        # run same command using pool, container, dfs-path
        data = run_fs_get_attr(self.conf, pool, uns_container.id(),
                               '--dfs-path', '/d1')
        assert check_dir_attr(data, 'S1', None, 'S1', 1048576)

        data = run_fs_get_attr(self.conf, '--path', file1)
        assert check_file_attr(data, None, 1048576)

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
        data = run_fs_get_attr(self.conf, '--path', dir1)
        assert check_dir_attr(data, 'S1', 'S2', 'S2', 16)

        data = run_fs_get_attr(self.conf, '--path', file2)
        assert check_file_attr(data, 'S1', 16)

        # Progressive-layout (PL) coverage.  PL is gated behind a large target-count minimum which
        # a single NLT server does not meet, so bypass the gate with DFS_PL_BYPASS_TARGET_LIMIT.
        # A dfuse and daos command started while this is set inherit it via get_base_env().  With
        # this server's 4 targets the default byte-array class (SX) resolves to S4, so a
        # default-class file gets a compact S1 head object plus an S4 tail segment.  The small NLT
        # pool makes the computed split point fall below DFS_PL_SPLIT_OFF_MIN, so it is clamped to
        # that minimum of 64 MiB.
        pl_split_off = 64 * 1024 * 1024
        os.environ['DFS_PL_BYPASS_TARGET_LIMIT'] = '1'
        try:
            pl_dfuse = DFuse(self.server, self.conf, container=uns_container, caching=False)
            pl_dfuse.use_valgrind = False
            pl_dfuse.start(v_hint='daos_fs_tool_pl')
            try:
                pl_dir = join(pl_dfuse.dir, 'pl_dir')
                os.mkdir(pl_dir)
                pl_file = join(pl_dir, 'pl_file')
                with open(pl_file, 'w'):
                    pass

                # The directory template advertises the S1 head and S4 tail for default files.
                data = run_fs_get_attr(self.conf, '--path', pl_dir)
                assert check_dir_pl_attr(data, 'S1', 'S4', pl_split_off), data

                # The default-class file is created with an S1 head and an S4 tail segment.
                data = run_fs_get_attr(self.conf, '--path', pl_file)
                assert check_file_pl_attr(data, 'S1', 'S4', pl_split_off), data
            finally:
                if pl_dfuse.stop():
                    self.fatal_errors = True
        finally:
            del os.environ['DFS_PL_BYPASS_TARGET_LIMIT']

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
               f'daos://{self.pool.id()}/{self.container.id()}']
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
        if 'DER_BUSY(-1012): Device or resource busy' not in line[-1]:
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
        data = run_fs_get_attr(self.conf, '--path', fname1)
        assert check_file_attr(data, None, 1048576)
        with open(fname1, 'r', encoding='ascii', errors='ignore') as fd:
            data = fd.read()
            if data != 'test1':
                print('/test_dir/f1 data is corrupted')

        data = run_fs_get_attr(self.conf, '--path', fname3)
        assert check_file_attr(data, None, 1048576)
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

        # hexdump to check file
        self.server.run_daos_client_cmd_pil4dfs(['hexdump', file1])

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
        # cp "/usr/bin/mkdir" to DFS and call "/usr/bin/file" to analyze the binary file file6
        file6 = join(path, 'elffile')
        self.server.run_daos_client_cmd_pil4dfs(['cp', '/usr/bin/mkdir', file6])
        self.server.run_daos_client_cmd_pil4dfs(['file', file6])

    @needs_dfuse_with_opt(caching_variants=[False], ro=True)
    def test_mount_ro(self):
        """Check that mounting read-only does not allow write access"""
        test_file = join(self.dfuse.dir, 'test_file')
        try:
            with open(test_file, 'w'):
                assert False
        except OSError as error:
            if error.errno == errno.EROFS:
                return
            raise

    def import_torch(self, server):
        """Return a handle to the pydaos.torch module"""
        os.environ['D_LOG_MASK'] = 'INFO'
        os.environ['DAOS_AGENT_DRPC_DIR'] = server.agent_dir

        return importlib.import_module('pydaos.torch')


def run_posix_tests(server, conf, test_list):
    """Run one or all posix tests

    Create a new container per test, to ensure that every test is
    isolated from others.
    """

    def _run_test(ptl=None, function=None, test_cb=None):
        ptl.call_index = 0
        set_active_test(function)
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

    test_list = list(test_list)
    pool = server.get_test_pool_obj()

    out_wrapper = NltStdoutWrapper()
    err_wrapper = NltStderrWrapper()

    pto = PosixTests(server, conf, pool=pool)
    if len(test_list) == 1:
        obj = getattr(pto, test_list[0])

        _run_test(ptl=pto, test_cb=obj, function=test_list[0])
    else:

        threads = []

        slow_tests = ['test_uns_basic', 'test_daos_fs_tool', 'manual_stable_cont_inode']

        test_list.sort(key=lambda x: x not in slow_tests)

        for function in test_list:
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
