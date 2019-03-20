#!/usr/bin/env python3
# Copyright (C) 2016-2019 Intel Corporation
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted for any purpose (including commercial purposes)
# provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice,
#    this list of conditions, and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions, and the following disclaimer in the
#    documentation and/or materials provided with the distribution.
#
# 3. In addition, redistributions of modified forms of the source or binary
#    code must carry prominent notices stating that the original code was
#    changed and the date of the change.
#
#  4. All publications or advertising materials mentioning features or use of
#     this software are asked, but not required, to acknowledge that it was
#     developed by Intel Corporation and credit the contributors.
#
# 5. Neither the name of Intel Corporation, nor the name of any Contributor
#    may be used to endorse or promote products derived from this software
#    without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
# DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
# ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
# THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
"""
Test methods for creating I/O to a filesystem.

These methods are inhereted by both iof_test_local and iof_simple_test so the
are invoked on both simple and multi-node launch

"""

import os
import subprocess
import tabulate
import logging
import tempfile
import unittest
import stat
import shutil
import json
from socket import gethostname
from decimal import Decimal
import time
import iof_ionss_setup
import iof_ionss_verify
import iofcommontestsuite
#pylint: disable=import-error
#pylint: disable=no-name-in-module
from distutils.spawn import find_executable
#pylint: enable=import-error
#pylint: enable=no-name-in-module
import operator


IMPORT_MNT = None
CTRL_DIR = None

try:
    from colorama import Fore
    COLORAMA = True
except ImportError:
    COLORAMA = False

def import_list():
    """Return a array of imports

    This list will not be sorted, simply as returned by the kernel.

    For both current users of this file the first entry should be a empty,
    usable mount point.
    """

    imports = []
    entry = os.path.join(CTRL_DIR, "iof", "projections")
    for projection in os.listdir(entry):
        myfile = os.path.join(entry, projection, 'mount_point')
        with open(myfile, "r") as fd:
            mnt_path = fd.readline().strip()
        fd.close()
        imports.append(mnt_path)
    return imports

def get_writeable_import():
    """Returns a writeable import directory"""

    return import_list()[0]


#pylint: disable=too-many-public-methods
#pylint: disable=no-member

class ColorizedOutput():
    """Contains all output methods for using colorized output"""

    logger = logging.getLogger("TestRunnerLogger")
    stream = None
    have_errors = False

    def set_log(self, stream):
        """ Set a logfile stream """
        self.stream = stream

    def colour_output(self, colour, output, prefix=None):
        """Show output in colour"""
        if prefix:
            prefix = '{}: '.format(prefix)
        else:
            prefix = ''
        if colour and COLORAMA:
            self.logger.info(getattr(Fore, colour) + output)
            print(Fore.RESET, end="")
        else:
            self.logger.info(prefix + output)
        if self.stream:
            self.stream.write("{}{}\n".format(prefix, output))

    def success_output(self, output):
        """Green output to console, writes output to internals.out"""
        self.colour_output('GREEN', output)

    def error_output(self, output):
        """Red output to console, writes output to internals.out"""
        self.have_errors = True
        self.colour_output('RED', output, 'ERROR')

    def warning_output(self, output):
        """Yellow output to console, writes output to internals.out"""
        self.colour_output('YELLOW', output, 'WARNING')

    def normal_output(self, output):
        """Normal output to console, writes output to internals.out"""
        self.colour_output(None, output)

    def log_output(self, output):
        """Writes output to file"""
        if self.stream:
            self.stream.write("{}\n".format(output))

    def list_output(self, output_list):
        """Writes entire list of strings to internals.out, only output
        colorized warnings or errors to console by default"""
        if self.stream:
            for item in output_list:
                self.stream.write("{}\n".format(item))
        if not COLORAMA:
            return
        for item in output_list:
            try:
                (prefix, output) = item.split(' ', 1)
            except ValueError:
                prefix = None

            if prefix == 'ERROR:':
                self.logger.info(Fore.RED + output + Fore.RESET)

            elif prefix == 'WARN:':
                self.logger.info(Fore.YELLOW + output + Fore.RESET)

    def table_output(self, table, **kwargs):
        """Write a table to the logfile
        Accepts all the same options as tabulate itself, plus a title to be
        output before the table itself.
        """
        if not self.stream:
            return
        if 'title' in kwargs:
            self.stream.write('{}:\n'.format(kwargs['title']))
            del kwargs['title']
        output = tabulate.tabulate(table, **kwargs)
        self.stream.write(output)
        self.stream.write('\n')
        self.stream.write('\n')

class InternalsPathFramework(ColorizedOutput):
    """Contains all methods relating to internals path testing"""

    def verify_mount(self, mount_dir):
        """Compares /proc/mounts to see if expected mount point is really
        mounted for each IOF projection."""
        output = None
        condition = False
        path = "/proc/mounts"
        search_str = "IOF "
        mount_count = len(mount_dir)
        count = 0
        with open(path, 'r') as f:
            self.normal_output('\nVerifying FUSE Mount(s):')
            for line in f.readlines():
                if search_str in line:
                    fields = line.strip().split()
                    output = fields[1]
                    if output in mount_dir:
                        self.normal_output(line)
                        count += 1
                        if count == mount_count:
                            condition = True
                            break
        if condition:
            self.success_output('FUSE mount(s) match IOF projection(s)')
        else:
            self.error_output('Not all IOF projections currently '
                              'mounted')

    def verify_ionss(self, ctrl_fs_dir):
        """Returns ionss count and psr rank for given CNSS.
        For private access mode, ionss_count should be 1 and
        psr_rank should be 0."""
        self.normal_output('\nVerify IONSS:')
        ionss_count_path = os.path.join(ctrl_fs_dir, 'iof', 'ionss_count')
        with open(ionss_count_path, 'r') as g:
            ionss_count = g.read()
        psr_rank_path = os.path.join(ctrl_fs_dir, 'iof', 'ionss', '0',
                                     'psr_rank')
        with open(psr_rank_path, 'r') as h:
            psr_rank = h.read()
        self.normal_output('IONSS count = {}PSR rank = {}'.format(ionss_count,
                                                                  psr_rank))

    def dump_failover_state(self):
        """Log the current failover state, and return list"""

        states = []
        ctrl_fs_dir = CTRL_DIR

        projs_dir = os.path.join(ctrl_fs_dir, 'iof', 'projections')
        for proj in os.listdir(projs_dir):

            mount_point = None
            with open(os.path.join(projs_dir, proj, 'mount_point'), 'r') as f:
                mount_point = f.read().strip()

            state = 'unknown'
            with open(os.path.join(projs_dir, proj, 'failover_state'),
                      'r') as f:
                state = f.read().strip()
                states.append(state)

            self.normal_output("state for {} is '{}'".format(mount_point,
                                                             state))
        return states

    def dump_cnss_stats(self, ctrl_fs_dir):
        """Dumps CNSS stats/FUSE callbacks for each IOF projection.
        Returns a list containing projection mount point, a list of the names of
        current statistics being measured, as well as a list of the actual
        stats."""
        ret_stats = []
        projs_dir = os.path.join(ctrl_fs_dir, 'iof', 'projections')
        for proj in os.listdir(projs_dir):
            cnss_stats = []
            stats = []
            stats_dir = os.path.join(projs_dir, proj, 'stats')
            if not os.path.exists(stats_dir):
                return None

            mount_point = None
            with open(os.path.join(projs_dir, proj, 'mount_point'), 'r') as f:
                mount_point = f.read().strip()

            stats_list = sorted(os.listdir(stats_dir))
            cnss_stats.append(mount_point)
            cnss_stats.append(stats_list)
            for s in stats_list:
                with open(os.path.join(stats_dir, s), 'r') as g:
                    stats_calls = int(g.read())
                    stats.append(stats_calls)
            cnss_stats.append(stats)
            ret_stats.append(cnss_stats)
        if ret_stats is None:
            self.error_output('Error in dumping CNSS stats')
        return ret_stats

    def delta_cnss_stats(self, d, mnt):
        """Computes the delta of initial and final CNSS stats/FUSE callbacks for
        each IOF projection and displays non-zero results"""
        stats = []

        d['delta_cstats_{}'.format(mnt)] = list(map(operator.sub,
                                                    d['final_{}'.\
                                                      format(mnt)],
                                                    d['init_{}'.\
                                                      format(mnt)]))
        for i in range(len(d['stats_list_{}'.format(mnt)])):
            if d['delta_cstats_{}'.format(mnt)][i] != 0:
                stats.append([d['stats_list_{}'.format(mnt)][i],
                              float(d['delta_cstats_{}'.format(mnt)][i])])
        if not stats:
            return
        self.normal_output('stats delta for {}: '.format(mnt))
        self.normal_output(tabulate.tabulate(stats, floatfmt=",.0f"))

    def compare_projection_dir(self, mount_dirs, ionss_dirs, ionss_node):
        """Compare contents of projection directory on CN with the original
        directory on the ION using rsync.
        ionss_node = 'single node' if running on one node, otherwise passes
        ionss node"""
        self.normal_output('\nValidate Projection and FS Data:')
        if not ionss_dirs:
            self.error_output('IONSS directories are NULL')
            return
        for index, idir in enumerate(ionss_dirs):
            if idir == '/usr':
                break # issues with rsync on '/usr' dir
            if not mount_dirs:
                self.error_output('Projection directories are NULL')
                return
            mount_dir = os.path.join(mount_dirs[index], '')
            cmd = (['rsync', '-nvrc', '--links'])
            if ionss_node == 'single node':
                ionss_path = idir
            else:
                ionss_path = '{}:{}'.format(ionss_node, idir)
                cmd.extend(['-e', 'ssh -o StrictHostKeyChecking=no '
                            '-o UserKnownHostsFile=/dev/null'])
            cmd.extend([mount_dir, ionss_path])
            p1 = subprocess.Popen(cmd,
                                  stdout=subprocess.PIPE,
                                  stderr=subprocess.PIPE)
            # output from rsync command will be incremental file list
            # consisting of differing files between the given directories
            output, err = p1.communicate()
            err = err.decode('utf-8')
            if err != '':
                self.error_output(' '.join(cmd))
                self.error_output(err)
            else:
                output = output.decode('ascii')
                first_file = output.split("\n")[1]
                if first_file == '':
                    self.success_output('IOF projection:{} matches I/O backend'
                                        ' fs:{}'.format(mount_dir, idir))
                else:
                    self.error_output('IOF projection:{} and backend '
                                      'fs:{} differ.'.format(mount_dir, idir))

class CnssChecks(iof_ionss_verify.IonssVerify,
                 iofcommontestsuite.CommonTestSuite,
                 iof_ionss_setup.IonssExport):
    """A object purely to define test methods.

    These methods are invoked on a node where projections are being imported,
    so it can access the CNSS through the ctrl filesystem.

    There is no side-channel access to the exports however.

    Additionally, for multi-node these tests are executed in parallel so need
    to be carefull to use unique filenames and should endevour to clean up
    after themselves properly.

    This class imports from unittest to get access to the self.fail() method
    """

    logger = logging.getLogger("TestRunnerLogger")
    import_dir = None
    export_dir = None
    cnss_prefix = None
    test_local = None

    # Does this test leave the inode hash table in a inconsistent state. If
    # a test sets this to True then the hash table consistency check will
    # not be run on the logs after the test completes.  This logic should be
    # removed once the bug is fixed.
    htable_bug = False

    @staticmethod
    def get_unique(parent):
        """Return a unique identifer for this user of the projection

        Note that several methods within the same process will get the same
        unique identifier."""

        return tempfile.mkdtemp(dir=parent, prefix='%s_%d_' %
                                (gethostname().split('.')[0], os.getpid()))

    def test_chmod_file(self):
        """chmod a file"""

        self.logger.info("Creating chmod file  at %s", self.import_dir)
        filename = os.path.join(self.import_dir, 'chmod_file')
        init_mode = stat.S_IRUSR|stat.S_IWUSR
        fd = os.open(filename, os.O_RDWR|os.O_CREAT, init_mode)
        os.close(fd)
        fstat = os.stat(filename)

        self.logger.info("st_mode is 0%o", fstat.st_mode)

        actual_mode = stat.S_IMODE(fstat.st_mode)
        self.logger.info("0%o", actual_mode)

        if actual_mode != init_mode:
            self.fail("Mode is incorrect 0%o 0%o" % (actual_mode, init_mode))

        new_mode = stat.S_IRUSR
        os.chmod(filename, new_mode)
        fstat = os.stat(filename)
        self.logger.info("st_mode is 0%o", fstat.st_mode)

        actual_mode = stat.S_IMODE(fstat.st_mode)
        self.logger.info("0%o", actual_mode)

        if actual_mode != new_mode:
            self.fail("Mode is correct 0%o 0%o" % (actual_mode, new_mode))

    def test_fchmod(self):
        """Fchmod a file"""

        self.logger.info("Creating fchmod file  at %s", self.import_dir)
        filename = os.path.join(self.import_dir, 'fchmod_file')
        init_mode = stat.S_IRUSR|stat.S_IWUSR
        fd = os.open(filename, os.O_RDWR|os.O_CREAT, init_mode)

        fstat = os.fstat(fd)
        self.logger.info("st_mode is 0%o", fstat.st_mode)

        actual_mode = stat.S_IMODE(fstat.st_mode)
        self.logger.info("0%o", actual_mode)

        if actual_mode != init_mode:
            self.fail("Mode is incorrect 0%o 0%o" % (actual_mode, init_mode))

        new_mode = stat.S_IRUSR
        os.fchmod(fd, new_mode)
        fstat = os.fstat(fd)
        self.logger.info("st_mode is 0%o", fstat.st_mode)

        actual_mode = stat.S_IMODE(fstat.st_mode)
        self.logger.info("0%o", actual_mode)

        if actual_mode != new_mode:
            self.fail("Mode is incorrect 0%o 0%o" % (actual_mode, new_mode))
        os.close(fd)

    def test_file_copy(self):
        """Copy a file into a projecton"""

        # Basic copy, using large I/O.  No permissions or metadata are used.

        filename = os.path.join(self.import_dir, 'ls')

        shutil.copyfile('/bin/ls', filename)
        if self.test_local:
            self.verify_file_copy()

    def test_file_ftruncate(self):
        """Truncate a file"""

        filename = os.path.join(self.import_dir, 't_file')
        self.logger.info("test_file_ftruncate %s", filename)

        fd = open(filename, 'w')
        fd.truncate()

        fstat = os.stat(filename)
        if fstat.st_size != 0:
            self.fail("Initial size incorrect %d" % fstat.st_size)

        for size in [0, 100, 4, 0]:

            fd.truncate(size)
            fstat = os.stat(filename)

            if fstat.st_size != size:
                self.fail("File truncate to %d failed %d" % (size,
                                                             fstat.st_size))

        fd.close()

    def test_file_open_new(self):
        """Create a new file"""

        self.logger.info("Create a new file  at %s", self.import_dir)
        filename = os.path.join(self.import_dir, 'test_file2')

        fd = open(filename, 'w')
        fd.close()

        fstat = os.stat(filename)
        if not stat.S_ISREG(fstat.st_mode):
            self.fail("Failed to create a regular file")

    @unittest.skip("Test not complete")
    def test_file_open(self):
        """Open a file for reading"""

        # This is supposed to fail, as the file doesn't exist.

        filename = os.path.join(self.import_dir, 'non_exist_file')

        fd = open(filename, 'r')
        fd.close()

    def test_file_read_empty(self):
        """Read from a empty file"""

        filename = os.path.join(self.import_dir, 'empty_file')

        fd = open(filename, 'w')
        fd.close()

        fd = open(filename, 'r')

        if os.stat(filename).st_size != 0:
            self.fail("File is not empty.")

        fd.read()
        fd.close()

    def test_file_read_zero(self):
        """Read 0 bytes from a file"""

        tfile = os.path.join(self.import_dir, 'zero_file')

        fd = os.open(tfile, os.O_RDWR|os.O_CREAT)
        ret = os.read(fd, 10)
        if ret.decode():
            self.fail("Failed to return zero bytes"  %ret.decode())
        os.close(fd)

    def test_file_rename(self):
        """Write to a file"""

        self.htable_bug = True

        filename = os.path.join(self.import_dir, 'c_file')

        fd = open(filename, 'w')
        fd.write('World')
        fd.close()

        new_file = os.path.join(self.import_dir, 'd_file')
        os.rename(filename, new_file)

    def test_file_sync(self):
        """Sync a file"""

        filename = os.path.join(self.import_dir, 'sync_file')

        fd = os.open(filename, os.O_RDWR|os.O_CREAT)
        os.write(fd, bytes("Hello world", 'UTF-8'))
        os.fsync(fd)

        os.lseek(fd, 0, 0)
        data = os.read(fd, 100).decode('UTF-8')

        if data != 'Hello world':
            self.fail('File contents wrong %s' % data)
        else:
            self.logger.info("Contents read from the synced file: %s", data)

        os.ftruncate(fd, 100)
        os.close(fd)

    def test_file_truncate(self):
        """Write to a file"""

        filename = os.path.join(self.import_dir, 'truncate_file')

        fd = open(filename, 'w')
        fd.write('World')
        fd.close()

        fd = open(filename, 'w')
        fd.write('World')
        fd.close()

    def test_file_unlink(self):
        """Create and remove a file"""

        filename = os.path.join(self.import_dir, 'unlink_file')

        fd = open(filename, 'w')
        fd.close()
        os.unlink(filename)

    def test_mkdir(self):
        """Create a directory and check it exists"""

        # This test is also a bit of a nonsense as it makes a directory, however
        # it relies on mkdtemp() is get_unique() in order to launch, so if there
        # is a problem it'll be the setup which will fail, not the test.

        ndir = os.path.join(self.import_dir, 'new_dir')

        self.logger.info(self.id())

        self.logger.info("Creating new directory at %s", ndir)

        os.mkdir(ndir)

        if not os.path.isdir(ndir):
            self.fail("Newly created directory does not exist")

        self.logger.info(os.listdir(ndir))

        os.rmdir(ndir)

    def test_mnt_path(self):
        """Check that mount points do not contain dot characters"""

        for mnt in import_list():
            self.assertFalse('.' in mnt, 'mount point should not contain .')

    def test_rmdir(self):
        """Remove a directory"""

        ndir = os.path.join(self.import_dir, 'my_dir')

        os.mkdir(ndir)

        self.logger.info("Directory contents:")
        self.logger.info(os.listdir(ndir))

        os.rmdir(ndir)

    def test_set_time(self):
        """Set the time of a file"""

        filename = os.path.join(self.import_dir, 'time_file')

        fd = open(filename, 'w')
        fd.close()

        stat_info_pre = os.stat(filename)
        self.logger.info("Stat results before setting time:")
        self.logger.info(stat_info_pre)
        time.sleep(2)
        os.utime(filename)
        stat_info_post = os.stat(filename)
        self.logger.info("Stat results after setting time (sleep for 2s):")
        self.logger.info(stat_info_post)

        if stat_info_pre.st_mtime == stat_info_post.st_mtime:
            self.fail("File mtime did not change")
    # These methods have both multi node and iof_test_local component

    def test_file_copy_from(self):
        """Copy a file into a projection"""

        # Basic copy, using large I/O.  No permissions or metadata are used.

        if self.test_local:
            self.export_file_copy_from()

        filename = os.path.join(self.import_dir, 'ls')
        dst_file = os.path.join(self.export_dir, 'ls.2')
        self.logger.info("test_file_copy_from %s", filename)
        self.logger.info("test_file_copy_from to: %s", dst_file)

        shutil.copyfile(filename, dst_file)
        if self.test_local:
            self.verify_file_copy_from()

    def test_file_open_existing(self):
        """Open a existing file for reading"""

        if self.test_local:
            self.export_file_open_existing()

        filename = os.path.join(self.import_dir, 'exist_file')
        self.logger.info("test_file_open_existing %s", filename)
        fd = open(filename, 'r')
        fd.close()

    def test_file_read(self):
        """Read from a file"""

        if self.test_local:
            self.export_file_read()

        filename = os.path.join(self.import_dir, 'read_file')
        self.logger.info("test_file_read %s", filename)
        with open(filename, 'r') as fd:
            data = fd.read()

        if data != 'Hello':
            self.fail('File contents wrong %s %s' % ('Hello', data))
        else:
            self.logger.info("Contents from file read: %s %s", 'Hello', data)

    def test_file_write(self):
        """Write to a file"""

        filename = os.path.join(self.import_dir, 'write_file')
        with open(filename, 'w') as fd:
            fd.write('World')
            fd.close()

        if self.test_local:
            self.verify_file_write()

    def test_ionss_link(self):
        """CORFSHIP-336 Check that stat does not deference symlinks"""
        # Make a directory 'b', create a symlink from 'a' to 'b'
        # and then stat() it to see what type it is

        if self.test_local:
            self.export_ionss_link()

        e_b = os.lstat(os.path.join(self.import_dir, 'a'))
        if self.test_local:
            self.verify_clean_up_ionss_link()

        self.logger.info("test_ionss_link %s", e_b)
        self.logger.info(e_b)
        if stat.S_ISLNK(e_b.st_mode):
            self.logger.info("It's a link")
        elif stat.S_ISDIR(e_b.st_mode):
            self.logger.info("It's a dir")
            self.fail("File should be a link")
        else:
            self.fail("Not a directory or a link")

    def test_ionss_self_listdir(self):
        """Perform a simple listdir operation"""
        if self.test_local:
            self.export_ionss_self_listdir()

        dirs = os.listdir(self.import_dir)

        self.logger.info(dirs)
        if self.test_local:
            self.verify_clean_up_ionss_self_listdir()

    def test_make_symlink(self):
        """Make a symlink"""

        os.symlink('mlink_target',
                   os.path.join(self.import_dir, 'mlink_source'))
        self.logger.info(os.listdir(self.import_dir))

        if self.test_local:
            self.verify_make_symlink()

    def test_many_files(self):
        """Create lots of files, and then perform readdir"""

        if self.test_local:
            self.export_many_files()

        test_dir = os.path.join(self.import_dir, 'many')
        files = []
        for x in range(0, 100):
            this_file = 'file_%d' % x
            filename = os.path.join(test_dir, this_file)
            fd = open(filename, 'w')
            fd.close()
            files.append(this_file)

        import_list_files = os.listdir(test_dir)
        self.logger.info("test_many_files files %s", sorted(files))
        self.logger.info("test_many_files import_list_files %s",
                         sorted(import_list_files))
        # create a list of files to be checked in ionss verify
        file_list = os.path.join(self.import_dir, 'file_list')
        with open(file_list, 'w') as f:
            for item in sorted(files):
                f.write(item + '\n')

        if self.test_local:
            self.verify_many_files()

        if sorted(files) != sorted(import_list_files):
            self.fail("Import Directory contents are wrong")

    def test_read_symlink(self):
        """Read a symlink"""

        self.logger.info("List the files on CN")
        self.logger.info(os.listdir(self.import_dir))

        if self.test_local:
            self.export_read_symlink()

        rlink_source = os.path.join(self.import_dir, 'rlink_source')
        self.logger.info("stat rlink_source %s", os.lstat(rlink_source))
        result = os.readlink(rlink_source)
        self.logger.info("read rlink_source %s", result)

        if result != 'rlink_target':
            self.fail("Link target is wrong '%s'" % result)
        else:
            self.logger.info("Verified read on link target with source")

    def run_mdtest(self, count=10, iters=3, timeout=1200):
        """Run mdtest with specified parameters"""

        mdtest_cmdstr = "/testbin/mdtest/bin/mdtest"
        if not os.path.exists(mdtest_cmdstr):
            mdtest_cmdstr = "mdtest"
        mdtest_cmdstr = find_executable(mdtest_cmdstr)
        if not mdtest_cmdstr:
            self.skipTest('mdtest not installed')
        cmd = [mdtest_cmdstr, '-d', self.import_dir]

        cmd.extend(['-i', str(iters), '-I', str(count)])
        start_time = time.time()
        rtn = self.common_launch_cmd(cmd, timeout=timeout)
        elapsed = time.time() - start_time
        print('Mdtest returned %d in %.2f seconds' % (rtn, elapsed))
        return (rtn, elapsed)

    def test_mdtest(self):
        """Test mdtest"""
        icount = 10
        iiters = 3

        self.htable_bug = True

        (rtn, elapsed) = self.run_mdtest(count=icount, iters=iiters)
        if rtn != 0:
            self.fail("Mdtest test_failed, rc = %d" % rtn)
        if elapsed > 5:
            return
        if 'iof_simple' in self.id():
            (rtn, elapsed) = self.run_mdtest(count=500, iters=30)
        else:
            desired_time = 30
            per_rep_time = elapsed / (icount * iiters)
            rcount = 5
            riters = int((desired_time/per_rep_time)/rcount)
            print('Mdtest took %.2f per rep, doing %d iters' % (per_rep_time,
                                                                riters))
            (rtn, elapsed) = self.run_mdtest(count=rcount, iters=riters)
            if rtn != 0:
                self.fail("Mdtest test_failed, rc = %d" % rtn)
            per_rep_time = elapsed / (rcount*riters)
            riters = int((desired_time/per_rep_time)/rcount)
            print('Mdtest took %.2f per rep, doing %d iters' % (per_rep_time,
                                                                riters))
            (rtn, elapsed) = self.run_mdtest(count=rcount, iters=riters)
        if rtn != 0:
            self.fail("Mdtest test_failed, rc = %d" % rtn)

    def test_self_test(self):
        """Run self-test"""

        self_test = find_executable('self_test')
        if not self_test:
            cart_prefix = os.getenv("IOF_CART_PREFIX",
                                    iofcommontestsuite.CART_PREFIX)
            if not cart_prefix:
                self.skipTest('Could not find self_test binary')
            self_test = os.path.join(cart_prefix, 'bin', 'self_test')
            if not os.path.exists(self_test):
                self.fail('Could not find self_test binary %s', self_test)
        environ = os.environ
        environ['CRT_PHY_ADDR_STR'] = self.crt_phy_addr
        environ['OFI_INTERFACE'] = self.ofi_interface
        cmd = [self_test, '--singleton', '--path', self.cnss_prefix,
               '--group-name', 'IONSS', '-e' '0:0',
               '-r', '50', '-s' '0 0,0 128,128 0']

        log_top_dir = os.getenv("IOF_TESTLOG",
                                os.path.join(os.path.dirname(
                                    os.path.realpath(__file__)), 'output'))
        log_path = os.path.join(log_top_dir, self.logdir_name())

        if not os.path.exists(log_path):
            os.makedirs(log_path)
        cmdfileout = os.path.join(log_path, "self_test.out.log")
        cmdfileerr = os.path.join(log_path, "self_test.err.log")
        environ['D_LOG_FILE'] = os.path.join(log_path, 'self_test_cart.log')
        procrtn = -1
        try:
            with open(cmdfileout, mode='w') as outfile, \
                open(cmdfileerr, mode='w') as errfile:
                outfile.write("{!s}\n  Command: {!s} \n{!s}\n".format(
                    ("=" * 40), (" ".join(cmd)), ("=" * 40)))
                outfile.flush()
                procrtn = subprocess.call(cmd, timeout=5 * 60, env=environ,
                                          stdout=outfile, stderr=errfile)
        except (FileNotFoundError) as e:
            self.logger.info("Testnss: %s", \
                             e.strerror)
        except (IOError) as e:
            self.logger.info("Testnss: Error opening the log files: %s", \
                             e.errno)

        with open(cmdfileout, "r") as fd:
            for line in fd.readlines():
                print(line.strip())
        fd.close()

        if procrtn != 0:
            self.fail("cart self test failed: %s" % procrtn)

    def test_statfs(self):
        """Invoke statfs"""

        fail = None
        for test_dir in import_list():
            try:
                r = os.statvfs(test_dir)
                print(r)
            except OSError as e:
                print('Failed with errno %d' % e.errno)
                fail = e.errno
        if fail is not None:
            self.fail('Failed with errno %d' % fail)

    def test_use_ino(self):
        """Test that stat returns correct information"""

        filename = os.path.join(self.import_dir, 'test_ino_file')
        self.logger.info("test_use_ino %s", filename)

        fd = open(filename, 'w')
        fd.close()

        a = os.stat(filename)

        # Currently the FUSE plugin does not correctly report inodes
        # so currently there are differences

        cn_stats = {}
        for key in dir(a):
            if not key.startswith('st_'):
                continue
            elif key == 'st_dev':
                continue

            cn_stats[key] = str(Decimal(getattr(a, key)))

        stat_file = os.path.join(self.import_dir, 'cn_stat_output')
        with open(stat_file, 'w') as fd:
            json.dump(cn_stats, fd, indent=4, skipkeys=True)

        if self.test_local:
            self.verify_use_ino()
