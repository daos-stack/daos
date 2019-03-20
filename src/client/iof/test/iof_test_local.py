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
local iof cnss / ionss test

It's possible to launch this test by running:

python3 -m unittest -c iof_test_local

Or to launch a specific test by running:

python3 -m unittest -c iof_test_local.Testlocal.test_ionss_link

"""

import os
import sys
import stat
import time
import shutil
import errno
import getpass
import signal
import tabulate
import subprocess
import tempfile
import yaml
import logging
import unittest
#pylint: disable=import-error
#pylint: disable=no-name-in-module
from distutils.spawn import find_executable
#pylint: enable=import-error
#pylint: enable=no-name-in-module
import iofcommontestsuite
import common_methods
import rpctrace_common_methods
import iof_cart_logparse
import iof_cart_logtest

jdata = None

sys.path.append('install/TESTING/scripts')
try:
    #Python/C Shim
    #pylint: disable=import-error
    import iofmod
    have_iofmod = True
    #pylint: enable=import-error
except ImportError:
    have_iofmod = False

#pylint: disable=too-many-lines
#pylint: disable=too-many-public-methods
#pylint: disable=too-many-statements
#pylint: disable=too-many-instance-attributes
#pylint: disable=too-many-ancestors

valgrind_cnss_only = False
use_fixed_paths = False

def unlink_file(file_name):
    """Unlink a file without failing if it doesn't exist"""

    try:
        os.unlink(file_name)
    except FileNotFoundError:
        pass

def create_file(directory, fname):
    """Create a empty file in the directory"""

    fd = open(os.path.join(directory, fname), mode='w')
    fd.close()

class Testlocal(unittest.TestCase,
                common_methods.CnssChecks,
                iofcommontestsuite.CommonTestSuite,
                common_methods.InternalsPathFramework):
    """Local test"""

    proc = None
    cnss_prefix = None
    import_dir = None
    export_dir = None
    shutdown_file = None
    active_file = None
    log_mask = ""
    crt_phy_addr = ""
    ofi_interface = ""
    test_local = True
    export_dirs = []
    mount_dirs = []
    cnss_stats = {}
    ionss_config_file = None
    log_path = None
    internals_tracing = True
    ionss_count = 3
    cnss_valgrind = False
    ionss_valgrind = False
    failover_test = False

    @classmethod
    def setUpClass(cls):

        # Try to load the config from file, but do not abort if not found.
        # This will add orterun to PATH, avoiding any pre-run setup, however
        # still works with the run_test.sh script which sets the PATH
        # from the yml file.
        try:
            global jdata
            jdata = iofcommontestsuite.load_config()
        except FileNotFoundError:
            pass

    def using_valgrind(self):
        """Check if any part of the test is running under valgrind"""

        return self.cnss_valgrind or self.ionss_valgrind

    def is_running(self):
        """Check if the cnss is running"""

        data = "0"

        try:
            fd = open(self.active_file, 'r')
            data = fd.read()
            fd.close()
        except FileNotFoundError:
            return False

        if data.rstrip() == '1':
            return True
        return False

    def logdir_name(self):
        """create the log directory name"""

        # Append the test case to the log directory to get unique names.
        # Do this in a way that matches the dump_error_messages() logic
        # in the test runner so that on failure only failing methods are
        # shown.

        parts = self.id().split('.')
        method = parts[2]
        if method.startswith('test_'):
            method = method[5:]
        return os.path.join(parts[1], method)

#pylint: disable=too-many-branches
#pylint: disable=too-many-locals
    def setUp(self):
        """set up the test"""

        if self.logger.getEffectiveLevel() == logging.WARNING:
            self.logger.setLevel(logging.INFO)
            __ch = logging.StreamHandler(sys.stdout)
            self.logger.addHandler(__ch)

        self.logger.info('\nStarting for %s', self.id())

        # If valgrind is requested for a failover test then enable it on the
        # cnss only.  Running the ionss under valgrind for the failover tests
        # causes them to be skipped.
        # In addition only use the --continuous flag for failover.
        #
        # Note that running the failover tests under valgrind does not cause
        # test failures, as orterun ignores the return code, however they will
        # be picked up by Jenkins via the XML files.  For non-failover tests
        # the valgrind exit code will be picked up and the test failed that way.
        global valgrind_cnss_only
        test_name = self.id()
        if test_name.split('.')[2].startswith('test_failover'):
            self.failover_test = True
            valgrind_cnss_only = True

        # set the standalone test flag
        self.test_local = True
        # Allow the use of a custom temp directory.  This can be needed on
        # docker when /tmp is an overlay fs.
        export_tmp_dir = os.getenv("IOF_TMP_DIR", '/tmp')

        if use_fixed_paths:
            top_dir = os.path.join(export_tmp_dir, os.getlogin(), 'iof')
            self.cnss_prefix = os.path.join(top_dir, 'cnss')
            self.export_dir = os.path.join(top_dir, 'ionss')
            if not os.path.exists(self.cnss_prefix):
                os.makedirs(self.cnss_prefix)
            if not os.path.exists(self.export_dir):
                os.makedirs(self.export_dir)
        else:
            self.cnss_prefix = tempfile.mkdtemp(prefix='tmp_iof_test_import_',
                                                dir=export_tmp_dir)
            self.export_dir = tempfile.mkdtemp(prefix='tmp_iof_test_export_',
                                               dir=export_tmp_dir)

        self.import_dir = os.path.join(self.cnss_prefix, 'exp')
        common_methods.CTRL_DIR = os.path.join(self.cnss_prefix, '.ctrl')
        self.shutdown_file = os.path.join(common_methods.CTRL_DIR, 'shutdown')
        self.active_file = os.path.join(common_methods.CTRL_DIR, 'active')

        if use_fixed_paths:
            for idir in [common_methods.CTRL_DIR,
                         os.path.join(self.cnss_prefix, 'exp'),
                         os.path.join(self.cnss_prefix, 'usr')]:
                try:
                    os.stat(idir)
                except OSError as e:
                    if e.errno == errno.ENOENT:
                        pass
                    elif e.errno == errno.ENOTCONN:
                        print("Unmounting previous projection/mount at %s" %
                              idir)
                        subprocess.call(['fusermount', '-u', idir])
                    else:
                        raise

        if jdata:
            orterun = os.path.realpath(os.path.join(jdata['OMPI_PREFIX'],
                                                    'bin',
                                                    'orterun'))
        else:
            ompi_bin = os.getenv('IOF_OMPI_BIN', None)
            if ompi_bin:
                orterun = os.path.realpath(os.path.join(ompi_bin, 'orterun'))
            else:
                orterun = 'orterun'

        config = {"projections":
                  [{"full_path": self.export_dir},
                   {"full_path": '/usr'}]}
        config['projections'][0]['mount_path'] = 'exp'
        config['failover'] = 'disable'
        config['cnss_timeout'] = 5
        if self.failover_test:
            config['projections'][0]['failover'] = 'auto'

        config_file = tempfile.NamedTemporaryFile(suffix='.cfg',
                                                  prefix="ionss_",
                                                  dir=export_tmp_dir,
                                                  mode='w',
                                                  delete=False)
        self.ionss_config_file = config_file.name
        yaml.dump(config, config_file.file, default_flow_style=False)
        config_file.close()

        self.export_dirs = [self.export_dir, '/usr']

        log_top_dir = os.getenv("IOF_TESTLOG",
                                os.path.join(os.path.dirname(
                                    os.path.realpath(__file__)), 'output'))

        # Append the test case to the log directory to get unique names.
        # Do this in a way that matches the dump_error_messages() logic
        # in the test runner so that on failure only failing methods are
        # shown.

        self.log_path = os.path.join(log_top_dir, self.logdir_name())

        valgrind = iofcommontestsuite.valgrind_suffix(self.log_path)
        if valgrind:
            if not valgrind_cnss_only:
                self.ionss_valgrind = True
            self.cnss_valgrind = True

        default_log_mask = "DEBUG,CTRL=WARN"
        itracing = os.getenv("INTERNALS_TRACING", "no")
        if itracing == "yes":
            self.internals_tracing = True
            default_log_mask = "DEBUG,CTRL=WARN"
        self.log_mask = os.getenv("D_LOG_MASK", default_log_mask)
        self.crt_phy_addr = os.getenv("CRT_PHY_ADDR_STR", "ofi+sockets")
        self.ofi_interface = os.getenv("OFI_INTERFACE", "lo")

        if 'mdtest' in self.id():
            #turn off debugging for mdtest
            self.log_mask = "WARN"
            self.internals_tracing = False

        # Only enable tcp and self for internal ompi/pmix comms to avoid
        # any problems on nodes which have IB cards.
        # Allow oversubscription of the cores on the nodes, so tests are
        # not limited to available CPUs.
        cmd = [orterun,
               '--mca', 'btl', 'self,tcp',
               '--mca', 'rmaps_base_oversubscribe', '1',
               '--output-filename', self.log_path]

        if getpass.getuser() == 'root':
            cmd.append('--allow-run-as-root')
        if self.failover_test or valgrind:
            cmd.append('--continuous')
        cmd.extend(['-n', '1',
                    '-x', 'D_LOG_MASK=%s' % self.log_mask,
                    '-x', 'CRT_PHY_ADDR_STR=%s' % self.crt_phy_addr,
                    '-x', 'CRT_TIMEOUT=11',
                    '-x', 'OFI_INTERFACE=%s' % self.ofi_interface])
        cnss_file = os.path.join(self.log_path, 'cnss.log')
        unlink_file(cnss_file)
        cmd.extend(['-x', 'D_LOG_FILE=%s' % cnss_file])

        if self.cnss_valgrind:
            cmd.extend(valgrind)
        if jdata:
            cmd.append(os.path.join(jdata['PREFIX'], 'bin', 'cnss'))
        else:
            cmd.append('cnss')
        cmd.extend(['-p', self.cnss_prefix,
                    ':',
                    '-n', str(self.ionss_count),
                    '-x', 'CRT_PHY_ADDR_STR=%s' % self.crt_phy_addr,
                    '-x', 'OFI_INTERFACE=%s' % self.ofi_interface,
                    '-x', 'D_LOG_MASK=%s' % self.log_mask])

        ionss_file = os.path.join(self.log_path, 'ionss.log')
        unlink_file(ionss_file)
        cmd.extend(['-x', 'D_LOG_FILE=%s' % ionss_file])

        if self.ionss_valgrind:
            cmd.extend(valgrind)
        if jdata:
            cmd.append(os.path.join(jdata['PREFIX'], 'bin', 'ionss'))
        else:
            cmd.append('ionss')

        cmd.extend(['-c', config_file.name])

        self.proc = self.common_launch_process(cmd)

        if self.using_valgrind():
            waittime = 120
        else:
            waittime = 30
        elapsed_time = 0
        while not self.is_running():
            elapsed_time += 1
            if elapsed_time > waittime:
                self.fail("Could not detect startup in %s seconds" % waittime)
            if not self.check_process(self.proc):
                procrtn = self.common_stop_process(self.proc)
                self.fail("orterun process did not start %d" % procrtn)
            time.sleep(1)

        self.logger.info("Running")

        w_log_file = os.path.join(self.cnss_prefix, '.ctrl', 'write_log')
        self.wfd = open(w_log_file, 'w')

        self.mount_dirs = common_methods.import_list()

        self.verify_mount(self.mount_dirs)

        #Verify IONSS
        self.verify_ionss(common_methods.CTRL_DIR)

        #Initial CNSS stats
        ret_stats = self.dump_cnss_stats(common_methods.CTRL_DIR)
        for projection_stats in ret_stats:
            mnt = projection_stats[0]
            self.cnss_stats["stats_list_{}".format(mnt)] = projection_stats[1]
            self.cnss_stats["init_{}".format(mnt)] = projection_stats[2]

        self.mark_log('Starting test {}'.format(self.id()))

    def mark_log(self, msg):
        """Log a message to stdout and to the CNSS logs

        self.wfd is a file descriptor for the write_log file in the ctrl
        filesystem, anything written to it gets output to the CaRT log
        for the cnss directly.
        """

        self.normal_output(msg)
        self.wfd.seek(0)
        self.wfd.write(msg)
        self.wfd.flush()
        os.fsync(self.wfd.fileno())

    def _log_check(self):
        """Check log files for consistency


        Always run the log file tracing, but have the internals (RPC)
        tracing be optional.

        The way this function is written is intended to allow most
        checks to run and report failures, even if some tests fail,
        so that when there is a failure we give as much inforation
        about is as possible.

        This isn't quite complete as if the CNSS log shows problems in
        the check_log_file() function the IONSS log won't be tested.
        """

        cl = iof_cart_logparse.IofLogIter(os.path.join(self.log_path,
                                                       'cnss.log'))

        il = None
        ionss_file = os.path.join(self.log_path, 'ionss.log')
        if os.path.exists(ionss_file):
            il = iof_cart_logparse.IofLogIter(ionss_file)

        cnss_errors = False
        ionss_errors = False
        missing_links = []

        if self.internals_tracing:
            #Create a dump file for all testing and internals path output
            internals_log_file = open(os.path.join(self.log_path,
                                                   'internals.log'),
                                      'w')

            #Origin RPC tracing for one CNSS instance
            ctrace = rpctrace_common_methods.RpcTrace(cl, internals_log_file)
            cnss_pid = ctrace.pids[0]
            ctrace.rpc_reporting(cnss_pid)

            #Target RPC tracing for multiple IONSS instances
            if il:
                itrace = rpctrace_common_methods.RpcTrace(il,
                                                          internals_log_file)
                for pid in itrace.pids:
                    itrace.rpc_reporting(pid)

            #Descriptor tracing for the CNSS
            descriptor = ctrace.descriptor_rpc_trace(cnss_pid)

            if descriptor is not None:
                missing_links = ctrace.rpc_trace_output(descriptor)

            internals_log_file.close()

            cnss_errors = ctrace.have_errors

            if il and not self.failover_test:
                ionss_errors = itrace.have_errors

        c_log_test = iof_cart_logtest.LogTest(cl)
        c_log_test.set_warning_function('drop_ino_ref', 'IOF-888')

        if not self.htable_bug:
            c_log_test.set_warning_function('iof_deregister_fuse', 'IOF-888')
            c_log_test.set_warning_function('ioc_forget_one', 'IOF-888')

        c_log_test.set_error_ok('src/common/iof_bulk.c')
        strict_test = True
        if self.failover_test or self.htable_bug:
            strict_test = False

        c_log_test.check_log_file(strict_test)

        # Don't check the server log files on failover, as inherently
        # they'll be incomplete.
        if il and not self.failover_test:

            i_log_test = iof_cart_logtest.LogTest(il)
            i_log_test.set_error_ok('src/common/iof_bulk.c')
            i_log_test.check_log_file(True)

        if self.internals_tracing:
            if cnss_errors:
                self.fail("Cnss log has integrity errors")
            if ionss_errors:
                self.fail("IONSS log has integrity errors")
            if missing_links:
                self.fail('Missing links for TRACE macros: %s' % missing_links)
#pylint: enable=too-many-branches

    def tearDown(self):
        """tear down the test"""

        self.dump_failover_state()

        self.mark_log('Finished test {}, cleaning up'.format(self.id()))

        self.wfd.close()

        self.clean_export_dir()

        #Final CNSS stats
        ret_stats = self.dump_cnss_stats(common_methods.CTRL_DIR)
        for projection_stats in ret_stats:
            mnt = projection_stats[0]

            self.cnss_stats["final_{}".format(mnt)] = projection_stats[2]

            self.delta_cnss_stats(self.cnss_stats, mnt)

        # Compare projection and FS
        # error with using rsync to compare '/usr' directory
        self.compare_projection_dir(self.mount_dirs, self.export_dirs,
                                    'single node')

        # Firstly try and shutdown the filesystems cleanly
        if self.is_running():
            f = open(self.shutdown_file, 'w')
            f.write('1')
            f.close()

        procrtn = 0
        # Now try to kill the orterun process
        if self.proc is not None:
            procrtn = self.common_stop_process(self.proc)

        self._log_check()

        self.cleanup(procrtn)

        self.normal_output("Ending {}".format(self.id()))

    def _tidy_callgrind_files(self):
        if self.cnss_valgrind:
            range_start = 0
        else:
            range_start = 1
        proc_count = 1
        if self.ionss_valgrind:
            proc_count += self.ionss_count
        for c_file_idx in range(range_start, proc_count):
            file_in = os.path.join(self.log_path,
                                   'callgrind-{}.in'.format(c_file_idx))
            file_out = os.path.join(self.log_path,
                                    'callgrind-{}.out'.format(c_file_idx))
            cmd = ['callgrind_annotate', '--auto=yes', file_in]
            with open(file_out, 'w') as f:
                subprocess.call(cmd, timeout=180, stdout=f)

    def cleanup(self, procrtn):
        """Delete any temporary files or directories created"""

        # Call fusermount on mount points in case there are stale mounts.
        # Remove the mount directories
        for mount in ['.ctrl', 'usr', 'exp']:
            mp = os.path.join(self.cnss_prefix, mount)
            try:
                self.common_launch_cmd(['fusermount', '-q', '-u', mp])
            except FileNotFoundError:
                pass
            os.rmdir(mp)

        # Finally, remove any temporary files created.
        os.unlink(os.path.join(self.cnss_prefix, 'IONSS.attach_info_tmp'))
        os.unlink(self.ionss_config_file)
        os.rmdir(self.cnss_prefix)
        shutil.rmtree(self.export_dir)

        print("Log dir is %s" % self.log_path)

        use_valgrind = os.getenv('TR_USE_VALGRIND', default=None)
        if use_valgrind == 'callgrind':
            self._tidy_callgrind_files()

        for dir_path, _, file_list in os.walk(self.log_path, topdown=False):
            for fname in file_list:
                full_path = os.path.join(dir_path, fname)
                try:
                    fstat = os.stat(full_path)
                    if fstat.st_size == 0:
                        os.unlink(full_path)
                        self.logger.debug("Deleted %s", full_path)
                except FileNotFoundError:
                    pass
            try:
                os.rmdir(dir_path)
                self.logger.debug("Removed %s", dir_path)
            except OSError:
                pass

        # Now exit if there was an error after the rest of the cleanup has
        # completed.
        if procrtn == 42:
            self.error_output("Job completed with valgrind errors")
            self.fail("Job completed with valgrind errors")
        elif procrtn != 0:
            self.error_output("Non-zero exit code from orterun {}".\
                               format(procrtn))
            self.fail("Non-zero exit code from orterun %d" % procrtn)

    def test_ro_stat(self):
        """Test that stat works on read-only projections"""

        filename = os.path.join(self.cnss_prefix, 'usr', 'bin')

        b = os.stat(os.path.join('/usr', 'bin'))
        a = os.stat(filename)

        self.logger.info(a)
        self.logger.info(b)

        diffs = []

        for key in dir(a):
            if not key.startswith('st_'):
                continue
            av = getattr(a, key)
            bv = getattr(b, key)
            self.logger.info("Key %s import %s export %s", key, av, bv)
            if key == 'st_dev':
                continue
            if av != bv:
                self.logger.error("Keys are differnet")
                diffs.append(key)

        if diffs:
            self.fail("Stat attributes are different %s" % diffs)

    def test_ro_link(self):
        """Test that stat works on read-only projections"""

        # Find the smallest file in the range of 1MB to 8MB.
        target_file = None
        u_stat = os.stat('/usr')
        bin_files = sorted(os.listdir('/usr/bin'))
        for bfile in bin_files:
            fname = os.path.join('/usr/bin/', bfile)
            s = os.lstat(fname)
            if s.st_dev != u_stat.st_dev:
                self.skipTest("Inconsistent device for /usr files")
            if not stat.S_ISLNK(s.st_mode):
                continue
            target_file = bfile
            break

        if target_file is None:
            self.fail("Could not find file in /usr/bin")

        s_target = os.readlink(os.path.join('/usr/bin', target_file))

        self.logger.info("File is %s", target_file)
        self.logger.info("Target is %s", s_target)

        test_file = os.path.join(self.cnss_prefix, 'usr', 'bin', target_file)
        ro_target = os.readlink(test_file)

        self.logger.info("Read-only target is %s", s_target)

        if ro_target != s_target:
            self.fail("Link target is wrong, %s %s" % (s_target, ro_target))

    def test_direct_read(self):
        """Read a large file"""

        # Find the smallest file in the range of 1MB to 8MB.
        target_file = None
        target_file_size = 1024 * 1024 * 8
        u_stat = os.stat('/usr')
        bin_files = sorted(os.listdir('/usr/bin'))
        for bfile in bin_files:
            fname = os.path.join('/usr/bin/', bfile)
            s = os.lstat(fname)
            if s.st_dev != u_stat.st_dev:
                self.skipTest("Inconsistent device for /usr files")
            if not stat.S_ISREG(s.st_mode):
                continue
            if not os.access(fname, os.R_OK):
                continue
            if s.st_size < 1024 * 1024:
                continue
            if s.st_size < target_file_size:
                target_file = bfile
                target_file_size = s.st_size

        if target_file is None:
            self.fail("Could not find file in /usr/bin")

        self.logger.info('Test file is %s %d', target_file, target_file_size)
        test_file = os.path.join(self.cnss_prefix, 'usr', 'bin', target_file)

        cmd = ['dd',
               'if=%s' % test_file,
               'of=/dev/null',
               'bs=64k',
               'iflag=direct']
        rtn = self.common_launch_cmd(cmd)
        if rtn != 0:
            self.fail('DD returned error')

    def test_direct_write(self):
        """Write a large file"""
        test_file = os.path.join(self.import_dir, 'test_file')

        cmd = ['dd',
               'if=/dev/zero',
               'of=%s' % test_file,
               'bs=4k',
               'count=8',
               'oflag=direct']

        rtn = self.common_launch_cmd(cmd)
        if rtn != 0:
            self.fail('DD returned error')

    def test_file_basic_write(self):
        """Write to a existing file"""

        create_file(self.export_dir, 'write_file')

        filename = os.path.join(self.import_dir, 'write_file')
        with open(filename, 'w') as fd:
            fd.write('World')
            fd.close()

    def test_ro_listdir(self):
        """Read directory contents"""

        test_dir = os.path.join(self.cnss_prefix, 'usr', 'bin')
        files = os.listdir(test_dir)

        if not files:
            self.fail("No files in /usr/bin")

    def test_large_read(self):
        """Read a large file"""

        # Find the smallest file in the range of 1MB to 8MB.
        target_file = None
        target_file_size = 1024 * 1024 * 8
        u_stat = os.stat('/usr')
        bin_files = sorted(os.listdir('/usr/bin'))
        for bfile in bin_files:
            fname = os.path.join('/usr/bin/', bfile)
            s = os.lstat(fname)
            if s.st_dev != u_stat.st_dev:
                self.skipTest("Inconsistent device for /usr files")
            if not stat.S_ISREG(s.st_mode):
                continue
            if not os.access(fname, os.R_OK):
                continue
            if s.st_size < 1024 * 1024:
                continue
            if s.st_size < target_file_size:
                target_file = bfile
                target_file_size = s.st_size

        if target_file is None:
            self.fail("Could not find file in /usr/bin")

        self.logger.info('Test file is %s %d', target_file, target_file_size)
        test_file = os.path.join(self.cnss_prefix, 'usr', 'bin', target_file)

        cmd = ['dd', 'if=%s' % test_file, 'of=/dev/null', 'bs=4k']
        rtn = self.common_launch_cmd(cmd)
        if rtn != 0:
            self.fail('DD returned error')

        cmd = ['dd', 'if=%s' % test_file, 'of=/dev/null', 'bs=64k']
        rtn = self.common_launch_cmd(cmd)
        if rtn != 0:
            self.fail('DD bs=65k returned error')

    def test_file_read_rename(self):
        """Read from a file which has been renamed on the backend"""

        # Create a file on the export location.
        tfile = os.path.join(self.export_dir, 'a_file')

        fd = open(tfile, 'w')
        fd.write("Hello")
        fd.close()

        # Open it through the projection
        filename = os.path.join(self.import_dir, 'a_file')
        fd = open(filename, 'r')

        # Rename it on the backend, so any FUSE cache is out of sync
        os.rename(tfile, os.path.join(self.export_dir, 'b_file'))

        # Now read and check the data.
        data = fd.read()
        fd.close()

        if data != 'Hello':
            self.fail('File contents wrong %s %s' % ('Hello', data))

    def test_ioil(self):
        """Run the interception library test"""
        # Check the value of il_ioctl before execution
        stats_dir = os.path.join(self.cnss_prefix, '.ctrl', 'iof',
                                 'projections', '0', 'stats')

        if not os.path.exists(stats_dir):
            self.fail("Stats dir missing.")

        if not os.path.isfile(os.path.join(stats_dir, 'il_ioctl')):
            self.fail("il_ioctl missing in stats.")
        else:
            stat_file = os.path.join(stats_dir, 'il_ioctl')
            f = open(stat_file, 'r')
            data = f.read()
            f.close()
            initial_il_ioctl = data.rstrip("\n")
            self.logger.info("initial il_ioctl: %s", initial_il_ioctl)

        dirname = os.path.dirname(os.path.realpath(__file__))
        test_path = os.path.join(dirname, '..', 'tests')

        if not os.path.exists(test_path):
            test_path = os.path.join(dirname, '..', 'install', os.uname()[0],
                                     'TESTING', 'tests')

        test_valgrind = iofcommontestsuite.valgrind_suffix(self.log_path,
                                                           pmix=False)

        environ = os.environ
        environ['D_LOG_MASK'] = self.log_mask
        environ['CRT_PHY_ADDR_STR'] = self.crt_phy_addr
        environ['OFI_INTERFACE'] = self.ofi_interface
        for tname in ['s_test_ioil', 'lf_s_test_ioil']:
            testname = os.path.join(test_path, tname)
            if not os.path.exists(testname):
                self.skipTest("%s executable not found" % tname)

            ioil_file = os.path.join(self.log_path, '%s.log' % tname)
            unlink_file(ioil_file)
            environ['D_LOG_FILE'] = ioil_file
            self.logger.info("libioil test - input string:\n %s\n", testname)
            # set this to match value used by this job
            cmd = []
            cmd.extend(test_valgrind)
            cmd.extend([testname])
            procrtn = subprocess.call(cmd, timeout=180,
                                      env=environ)

            if procrtn != 0:
                self.fail("IO interception test failed: %s" % procrtn)

        # Check the value of il_ioctl after execution
        f = open(stat_file, 'r')
        data = f.read()
        f.close()
        final_il_ioctl = data.rstrip("\n")
        self.logger.info("Final il_ioctl: %s", final_il_ioctl)
        # Compare il_ioctl value after the test run with initial value or
        # if il_ioctl value is less than 12 (expected value is atleast 12)
        if final_il_ioctl <= initial_il_ioctl or int(final_il_ioctl) < 12:
            self.fail("IO interception library failed to attach," + \
                " il_ioctl: %s" % final_il_ioctl)

    @unittest.skipUnless(have_iofmod, "needs iofmod")
    def test_readdir(self):
        """Test readdir through iofmod"""

        dirname = os.path.join(self.cnss_prefix, 'usr')

        dh = iofmod.opendir(dirname)

        if dh is not None:
            self.fail("Failed to open directory: '{}'".format(os.strerror(dh)))

        files = []
        while True:
            fname = iofmod.readdir()
            if fname is None:
                break

            self.logger.info(fname)

            if isinstance(fname, int):
                self.fail("readdir failed with {}".format(os.strerror(fname)))

            files.append(fname)

        rc = iofmod.rewinddir()
        if isinstance(rc, int):
            self.fail("Failed to rewind directory: '{}'".format \
                      (os.strerror(rc)))

        usr_files = os.listdir('/usr')
        self.logger.info('projected files: %s', files)
        self.logger.info('native files   : %s', usr_files)
        if files != usr_files:
            self.fail("Directory contents are wrong")

        rc = iofmod.closedir()

        if isinstance(rc, int):
            self.fail("Failed to close directory: '{}'".format(os.strerror(rc)))

    def test_double_stat(self):
        """Test a single file with two names"""

        # Create two files, stat them both, then rename one
        # after the other and rename the remaining file.

        self.htable_bug = True

        create_file(self.export_dir, 'a')
        create_file(self.export_dir, 'b')

        print(os.stat(os.path.join(self.import_dir, 'a')))
        print(os.stat(os.path.join(self.import_dir, 'b')))

        os.rename(os.path.join(self.export_dir, 'b'),
                  os.path.join(self.export_dir, 'a'))

        print(os.stat(os.path.join(self.import_dir, 'a')))

    def test_stat_no_file(self):
        """Test stat of a file that does, then does not exist"""

        create_file(self.export_dir, 'a_file')

        print(os.stat(os.path.join(self.import_dir, 'a_file')))

        os.unlink(os.path.join(self.export_dir, 'a_file'))

        try:
            print(os.stat(os.path.join(self.import_dir, 'a_file')))
        except FileNotFoundError:
            return
        self.fail('Should have failed')

#pylint: disable=too-many-branches
    @unittest.skipUnless(have_iofmod, "needs iofmod")
    def test_failover_readdir(self):
        """Test readdir with failover through iofmod"""

        # Firstly make a subdirectory, so we're not dealing directly with
        # the import directory.
        dirname = os.path.join(self.import_dir)
        for d in range(0, 5):
            dirname = os.path.join(dirname, str(d))
            os.mkdir(dirname)
        self.logger.info("Directory is %s", dirname)

        # Then make some files in it, just so there is something to observe.
        for f in range(0, 5):
            create_file(dirname, str(f))

        # Now open a handle which will be used throughout.
        dh = iofmod.opendir(dirname)
        if dh is not None:
            self.fail("Failed to open directory: '{}'".format(os.strerror(dh)))

        # Read the directory listing, aborting on any error
        files = []
        while True:
            fname = iofmod.readdir()
            if fname is None:
                break

            if isinstance(fname, int):
                self.fail("readdir failed with {}".format(os.strerror(fname)))

            files.append(fname)
        self.logger.info(files)

        # Now kill a server, which won't yet trigger failover.
        self.kill_ionss_proc()

        # Rewind a directory, and try to read it.  The first call to readdir
        # should trigger failover.
        rc = iofmod.rewinddir()
        if isinstance(rc, int):
            self.fail("Failed to rewind directory: '{}'".format \
                      (os.strerror(rc)))

        nfiles = []
        while True:
            fname = iofmod.readdir()
            if fname is None:
                break

            if isinstance(fname, int):
                if fname != errno.EHOSTDOWN:
                    self.fail("readdir failed with {}".format \
                              (os.strerror(fname)))
                break

            # As the code is not written yet check we got the expected result.
            self.fail("Should have got EHOSTDOWN")

            nfiles.append(fname)

        self.logger.info(nfiles)

        # Rewind a directory, and read it again.  This will cause another
        # attempt to send a RPC which should be rejected before it's sent
        # this time, so in the case of failure will take a different
        # codepath to above, but the result should be the same.
        rc = iofmod.rewinddir()
        if isinstance(rc, int):
            self.fail("Failed to rewind directory: '{}'".format \
                      (os.strerror(rc)))

        while True:
            fname = iofmod.readdir()
            if fname is None:
                break

            if isinstance(fname, int):
                if fname != errno.EHOSTDOWN:
                    self.fail("readdir failed with {}".format \
                              (os.strerror(fname)))
                break

            # As the code is not written yet check we got the expected result.
            self.fail("Should have got EHOSTDOWN")

            nfiles.append(fname)

        self.logger.info(nfiles)


        self.logger.info('projected files: %s', files)

        rc = iofmod.closedir()

        if isinstance(rc, int):
            self.fail("Failed to close directory: '{}'".format(os.strerror(rc)))
#pylint: enable=too-many-branches

    @unittest.skipUnless(have_iofmod, "needs iofmod")
    def test_failover_off_readdir(self):
        """Test failover readdir through iofmod"""

        # This tests a projection with failover disabled, after failover future
        # readdir calls should fail with EHOSTDOWN

        dirname = os.path.join(self.cnss_prefix, 'usr')

        dh = iofmod.opendir(dirname)

        if dh is not None:
            self.fail("Failed to open directory: '{}'".format(os.strerror(dh)))

        files = []
        while True:
            fname = iofmod.readdir()
            if fname is None:
                break

            self.logger.info(fname)

            files.append(fname)

        usr_files = os.listdir('/usr')
        self.logger.info('projected files: %s', files)
        self.logger.info('native files   : %s', usr_files)
        if files != usr_files:
            self.fail("Directory contents are wrong")

        rc = iofmod.rewinddir()
        if isinstance(rc, int):
            self.fail("Failed to rewind directory: '{}'".format \
                      (os.strerror(rc)))

        # Now kill a ionss so the next RPC will trigger failover.
        self.kill_ionss_proc()

        # Send a RPC, which should be sent, and then return an error.
        fname = iofmod.readdir()
        self.logger.info(fname)
        if isinstance(fname, int):
            self.logger.info("Failed with %s", os.strerror(fname))
            if fname != errno.EHOSTDOWN:
                self.fail("Should have got EHOSTDOWN")
        else:
            self.fail("Should have got EHOSTDOWN")

        # Make a new request, which should fail before it gets sent.
        fname = iofmod.readdir()
        self.logger.info(fname)
        if isinstance(fname, int):
            self.logger.info("Failed with %s", os.strerror(fname))
            if fname != errno.EHOSTDOWN:
                self.fail("Should have got EHOSTDOWN")
        else:
            self.fail("Should have got EHOSTDOWN")

        rc = iofmod.closedir()

        if isinstance(rc, int):
            self.fail("Failed to close directory: '{}'".format(os.strerror(rc)))

    @unittest.skipUnless(have_iofmod, "needs iofmod")
    def test_iofmod(self):
        """Calls all C tests present in C/Python shim"""

        if not have_iofmod:
            self.skipTest("iofmod not loadable")

        FD_TESTS = ['test_write_file', 'test_read_file']

        subtest_count = 0
        for possible in sorted(dir(iofmod)):
            if not possible.startswith('test_'):
                continue

            obj = getattr(iofmod, possible)
            if not callable(obj):
                continue

            subtest_count += 1
            with self.subTest(possible):
                self.mark_log('Starting test iofmod.%s' % possible)
                try:
                    self.clean_export_dir()
                except PermissionError:
                    self.skipTest("Unable to clean import dir")
                try:
                    if possible in FD_TESTS:
                        fd = iofmod.open_test_file(self.import_dir)
                        if fd is None:
                            self.fail('File descriptor returned null on open')

                        ret = obj(fd)
                        self.logger.info("%s returned %s", possible, str(ret))
                        if ret is None:
                            iofmod.close_test_file(fd)
                            self.fail('%s returned null' % possible)

                        # This will raise an exception of failure.
                        iofmod.close_test_file(fd)

                    else:
                        ret = obj(self.import_dir)
                        self.logger.info("%s returned %s", possible, ret)
                finally:
                    self.mark_log('Finished test iofmod.%s' % possible)

        return subtest_count

    def kill_ionss_proc(self):
        """ Kill a ionss process.

        Kill the ionss process which is PSR for the first client.
        """

        # Check if the ionss processes are running under valgrind, and if so
        # skip this test, the /proc parsing does not work in this case.
        # The --cnss-valgrind option should continue to work however.
        if self.ionss_valgrind:
            self.skipTest('Does not support IONSS processes under valgrind')

        procs = os.listdir('/proc')
        iprocs = []
        for proc in procs:
            ppath = '/proc/%s/exe' % proc
            if not os.path.exists(ppath):
                continue

            dest = os.readlink(ppath)
            exe = os.path.basename(dest)
            if exe != 'ionss':
                continue

            iprocs.append(int(proc))

        self.logger.info('pids are %s', iprocs)
        if len(iprocs) != self.ionss_count:
            self.fail("Could not find correct number of processes")

        os.kill(iprocs[0], signal.SIGINT)
        while os.path.exists('/proc/%d' % iprocs[0]):
            time.sleep(1)

    def test_failover_fstat(self):
        """Test open file migration during failover"""

        # This test creates a file on the 'exp' projection export point,
        # then opens it via IOF, triggers failover and then tries to
        # call fstat on the file.

        e_dir = os.path.join(self.export_dir, 'tdir')
        os.mkdir(e_dir)
        create_file(e_dir, 'tfile')

        ffile = os.path.join(self.import_dir, 'tdir', 'tfile')

        fd = os.open(ffile, os.O_RDONLY)

        s = os.fstat(fd)
        print(s)

        self.kill_ionss_proc()
        for i in range(0, 2):
            print("Loop %d" % i)
            fs = os.fstat(fd)
            print(fs)
        os.close(fd)

    def test_failover_fd_leak(self):
        """Test failover with open files"""

        # Check that files are correctly present in the inode hash table
        # after failover.  There is currently a bug here, so add a test
        # to check for that, but disable it via the htable_bug so that the
        # check can be enabled for other tests.

        self.htable_bug = True
        ffile = os.path.join(self.import_dir, 'tfile')
        fd = os.open(ffile, os.O_CREAT)
        os.fstat(fd)
        self.kill_ionss_proc()
        os.fstat(fd)

    def ft_stat_helper(self, fsid, outcomes):
        """Helper function for trying stat on projection

        fsid is the index of the projection to test.
        outcomes is an array of expected error numbers, or
        None for no error.
        """

        test_dir = self.mount_dirs[fsid]
        failed = False

        for expected in outcomes:
            try:
                self.dump_failover_state()
                self.logger.info("Reading %s", test_dir)
                r = os.stat(test_dir)
                self.logger.info(r)
                if expected is not None:
                    self.logger.info("stat succeeded but expected %d", expected)
                    failed = True
            except OSError as e:
                if e.errno == expected:
                    self.logger.info("stat returned errno %d '%s'",
                                     e.errno, e.strerror)
                else:
                    self.logger.info("stat returned errno %d '%s' expected %s",
                                     e.errno, e.strerror, expected)
                    failed = True
        return failed

    def test_failover_statfs(self):
        """Invoke statfs"""

        self.kill_ionss_proc()
        test_dir = self.import_dir
        try:
            r = os.statvfs(test_dir)
            print(r)
        except OSError as e:
            print('Failed with errno %d' % e.errno)
            self.fail('Failed with errno %d' % e.errno)

    def test_failover_stat(self):
        """Basic failover test"""

        # Launch IOF as normal, kill one IONSS process and then call stat
        # projection.  This should see EIO initially as the feature is not yet
        # complete however future requests should work correctly.

        self.dump_failover_state()
        self.kill_ionss_proc()

        # Now that the process is dead try performing I/O.  No wait is
        # required here, what should happen is the IONSS process set
        # is notified of the failure and performs a local eviction,
        # however no client notification happens until I/O is attempted
        # and tries out, so simply try some I/O at this point.

        # Iterate through the projection list to ensure that they fail with
        # the expected error codes.
        failed = self.ft_stat_helper(0, [None, None])

        self.dump_failover_state()

        if failed:
            self.fail('Failed with unexpected errno')

    def test_failover_off_stat(self):
        """Non-failover test"""

        # Try accessing a filesystem after failure when failover is disabled.
        # This should return EHOSTDOWN in all cases.
        self.dump_failover_state()
        self.kill_ionss_proc()

        failed = self.ft_stat_helper(1, [errno.EHOSTDOWN, errno.EHOSTDOWN])
        self.dump_failover_state()

        if failed:
            self.fail('Failed with unexpected errno')

    def ft_stat_many_helper(self, fd, expected):
        """Helper function for test_failover_many()"""
        if not expected:
            print(os.stat(fd))
            return

        try:
            print(os.stat(fd))
            self.fail("Should have failed")
        except OSError as e:
            if e.errno != expected:
                raise

    def test_failover_unlink(self, subdir=False):
        """Test unlink under failover"""

        idir = self.import_dir
        edir = self.export_dir
        if subdir:
            # If requested use a subdirectory for the test files.
            idir = os.path.join(self.import_dir, 'test_dir')
            edir = os.path.join(self.export_dir, 'test_dir')
            os.mkdir(edir)

        # Create two files on the backend so FUSE does not know about them.
        create_file(edir, 'u1')
        create_file(edir, 'u2')

        # Kill a server.
        self.kill_ionss_proc()

        # Test unlink where the request triggers eviction.
        os.unlink(os.path.join(idir, 'u1'))

        # Test unlink after failover.
        os.unlink(os.path.join(idir, 'u2'))

        files = os.listdir(idir)
        self.logger.info("Files are %s", files)

    def test_failover_unlink_subdir(self):
        """"Test unlink failover when the files are in a subdirectory"""

        self.htable_bug = True

        return self.test_failover_unlink(subdir=True)

    def test_failover_many(self):
        """Test failover with open files"""

        # Test various permutations of failover:
        # - Open a file in the top-level directory.
        # - Open a file in a subdirectory.
        # - Make a subdirectory, create a file in it and unlink the file.
        # - Open a file in the top-level directory, them remove it on the
        #   backend
        # - Open a file in the top-level directory, then remove and replace with
        #   alternate file on backend.
        # - Open a file, then close it.
        #
        # After failover then try the following:
        # Try to stat each open file.
        # Try to stat each subdirectory which has open files.
        # Try to stat a untouched, but pre-existing subdirectory.
        # Try to stat the previously created subdirectory
        #
        # Many of these cases are expected to fail currently (and removing the
        # file before failover always will) so the test is mostly to confirm the
        # code doesn't crash and to see progress on feature development.

        self.htable_bug = True

        frontend_dir = self.import_dir
        backend_dir = self.export_dir

        # Open a file.
        f1 = open(os.path.join(frontend_dir, 'f1'), mode='w')
        os.mkdir(os.path.join(frontend_dir, 'd1'))
        os.mkdir(os.path.join(frontend_dir, 'd2'))
        # Open a file which will remain open.
        f2 = open(os.path.join(frontend_dir, 'd1', 'f2'), mode='w')
        # Open a file, then remove it so d2 is empty.
        create_file(os.path.join(frontend_dir, 'd2'), 'f3')
        # Open a file which will be removed on the backend.
        f4 = open(os.path.join(frontend_dir, 'f4'), mode='w')
        os.unlink(os.path.join(backend_dir, 'f4'))
        # Open a file which will be overwritten on the backend.
        f5 = open(os.path.join(frontend_dir, 'f5'), mode='w')
        os.unlink(os.path.join(backend_dir, 'f5'))
        create_file(backend_dir, 'f6')
        os.rename(os.path.join(backend_dir, 'f6'),
                  os.path.join(backend_dir, 'f5'))
        create_file(backend_dir, 'f7')

        # Create a file which will be renamed before failover.
        create_file(frontend_dir, 'f8')
        os.rename(os.path.join(frontend_dir, 'f8'),
                  os.path.join(frontend_dir, 'f9'))

        print(os.fstat(f1.fileno()))
        print(os.fstat(f2.fileno()))
        print(os.fstat(f4.fileno()))
        print(os.fstat(f5.fileno()))
        print(os.stat(os.path.join(frontend_dir, 'd2')))
        print(os.stat(os.path.join(frontend_dir, 'f7')))

        # Now all the files are open kill a ionss process, and trigger failover.
        # Note this is the failover_stat() test.
        self.kill_ionss_proc()
        failed = self.ft_stat_helper(0, [None, None])
        self.dump_failover_state()

        if failed:
            self.fail('Failed with unexpected errno')

        self.ft_stat_many_helper(f1.fileno(), None)
        self.ft_stat_many_helper(f2.fileno(), None)
        self.ft_stat_many_helper(f4.fileno(), errno.EHOSTDOWN)
        self.ft_stat_many_helper(f5.fileno(), errno.EHOSTDOWN)
        self.ft_stat_many_helper(os.path.join(frontend_dir, 'd2'), None)
        self.ft_stat_many_helper(os.path.join(frontend_dir, 'f7'), None)

        f1.close()
        f2.close()
        f4.close()
        f5.close()

    def clean_export_dir(self):
        """Clean up files created in backend fs"""
        idir = os.path.join(self.export_dir)
        files = os.listdir(idir)
        for e in files:
            ep = os.path.join(idir, e)
            self.normal_output('Cleaning up {}'.format(ep))
            if os.path.isfile(ep) or os.path.islink(ep):
                os.unlink(ep)
            elif os.path.isdir(ep):
                shutil.rmtree(ep)
        files = os.listdir(idir)
        if files:
            self.error_output('Test left some files {}'.format(files))
            self.fail('Test left some files %s' % files)

    def go(self):
        """A wrapper method to invoke all methods as subTests"""

        # This method invokes all other test_* methods as subtests which has
        # the effect that they're all run on the same projection.  This is both
        # much faster but also means that it's more difficult to isolate the
        # tests.

        # After each test remove any left-over files from the export directory
        # to avoid using IOF for this.  Use the export rather than the import
        # point here so that problems in IOF are not reported against the test,
        # for example if there is a problem with unlink we do not want the
        # write tests to fail.
        # It also means that tests can interact with each other, for example
        # any kernel caching of entries or negative entries can cause issues
        # with tests not seeing the files they expect.  To avoid that use
        # unique files for each test.  What we should do is either setup a new
        # subdirectory under import/export for each test, or disable cacheing
        # in the kernel.
        subtest_count = 0
        for possible in sorted(dir(self)):
            if not possible.startswith('test_'):
                continue

            # Skip the iofmod methods as these are called directly later.
            if 'iofmod' in possible:
                continue

            if possible.startswith('test_failover_'):
                continue

            obj = getattr(self, possible)
            if not callable(obj):
                continue

            subtest_count += 1
            with self.subTest(possible[5:]):

                obj()
                self.clean_export_dir()

        if have_iofmod:
            subtest_count += self.test_iofmod()

        self.normal_output("Ran {} subtests".format(subtest_count))

def local_launch(ioil_dir):
    """Launch a local filesystem for interactive use"""

    fs = Testlocal()
    fs.setUp()

    print("IOF projections up and running")
    print(fs.cnss_prefix)
    print("Ctrl-C to shutdown or run")
    print("echo 1 > %s/.ctrl/shutdown" % fs.cnss_prefix)
    print("")
    print("To use the interception library, set ")
    print("LD_PRELOAD=%s/libioil.so or " % ioil_dir)
    print("link that library in your application before system libraries.  ")
    print("The latter option makes debugging simpler.")
    print("")
    my_rc = 0
    try:
        my_rc = fs.wait_process(fs.proc)
    except KeyboardInterrupt:
        print("Caught Ctrl-C")
        my_rc = 1
    fs.cleanup(my_rc)

    return my_rc

if __name__ == '__main__':
    import argparse

    # Invoke testing if this command is run directly
    #
    # Add command-line passing here so that the user can specify individual
    # tests to run easily, and to enable other configuration optins.
    #
    # An example command might be the following:
    #
    # ./iof_test_local.py --valgrind --log-mask=DEBUG --redirect chmod
    #
    # Unrecognised args are passed through to unittest.main().
    cnss_path = find_executable('cnss')
    if cnss_path:
        iof_root = os.path.dirname(os.path.dirname(cnss_path))
    else:
        jdata = iofcommontestsuite.load_config()
        print("cnss executable not found.  Loading congfiguration from json")
        iof_root = jdata['PREFIX']
    ioil_path = os.path.join(iof_root, 'lib')

    test_n = []
    test_f = []

    tests = []
    for ptest in sorted(dir(Testlocal)):
        if not ptest.startswith('test_'):
            continue

        t_obj = getattr(Testlocal, ptest)
        if not callable(t_obj):
            continue

        tests.append(ptest[5:])
        if ptest.startswith('test_failover_'):
            test_f.append([ptest[5:], t_obj.__doc__])
        else:
            test_n.append([ptest[5:], t_obj.__doc__])

    epilog_str = '\n'.join(['Available tests are:',
                            tabulate.tabulate(test_n),
                            tabulate.tabulate(test_f)])

    use_go = True
    skip_failover = False

    test_help = "A test to run (%s) or a utest argument" % ', '.join(tests)
    parser = argparse.ArgumentParser(description='Run local iof tests',
                                     epilog=epilog_str,
                                     formatter_class=\
                                     argparse.RawDescriptionHelpFormatter)
    parser.add_argument('tests', metavar='TEST', type=str,
                        help=test_help, nargs='*')
    parser.add_argument('--valgrind', action='store_true',
                        help='Run the test under valgrind')
    parser.add_argument('--cnss-valgrind', action='store_true',
                        help='Run the cnss process under valgrind')
    parser.add_argument('--callgrind', action='store_true',
                        help='Run the test under callgrind')
    parser.add_argument('--redirect', action='store_true',
                        help='Redirect daemon output to a file')
    parser.add_argument('--launch', action='store_true',
                        help='Launch a local file system for interactive use')
    parser.add_argument('--log-mask', dest='mask', metavar='MASK', type=str,
                        help='Set the CaRT log mask')
    parser.add_argument('--internals-tracing', action='store_true', help='Turn '
                        'on internals path testing w/ RPC/Descriptor tracing')
    parser.add_argument('--instance-per-test', action='store_true',
                        help='Run each test in clean instance')
    parser.add_argument('--fixed-path', action='store_true',
                        help='Use fixed paths for import/export')
    parser.add_argument('--skip-failover', action='store_true',
                        help="Skip the failover tests")
    args = parser.parse_args()

    if args.internals_tracing:
        os.environ['INTERNALS_TRACING'] = 'yes'

    if args.valgrind:
        os.environ['TR_USE_VALGRIND'] = 'memcheck-native'
    if args.cnss_valgrind:
        os.environ['TR_USE_VALGRIND'] = 'memcheck-native'
        valgrind_cnss_only = True
    if args.callgrind:
        os.environ['TR_USE_VALGRIND'] = 'callgrind'
    if args.redirect:
        os.environ['TR_REDIRECT_OUTPUT'] = 'yes'
    if args.fixed_path:
        use_fixed_paths = True
    if args.instance_per_test:
        use_go = False
    if args.skip_failover:
        skip_failover = True

    if args.mask:
        os.environ['D_LOG_MASK'] = args.mask

    tests_to_run = []
    uargs = []

    for test in args.tests:
        if test in tests:
            tests_to_run.append('Testlocal.test_%s' % test)
        else:
            uargs.append(test)

    if uargs:
        print("Unknown option given, passing through to unittest")
        print("Valid test names are %s" % ','.join(sorted(tests)))

    uargs.insert(0, sys.argv[0])

    if args.launch:
        launch_rc = local_launch(ioil_path)
        sys.exit(launch_rc)

    if not tests_to_run:
        # If no tests are specified then run all tests, however do this by
        # running regular tests as subtests of the 'go' test which will re-use
        # a single instance of IOF, and all failover tests individually with
        # a new IOF instance per test.
        # This allows reduced speed for most tests, however a full test run
        # to also include the failover tests.
        if use_go:
            tests_to_run.append('Testlocal.go')
        for ptest in sorted(dir(Testlocal)):
            if not callable(getattr(Testlocal, ptest)):
                continue
            if not skip_failover and ptest.startswith('test_failover_'):
                tests_to_run.append('Testlocal.%s' % ptest)
            if not use_go and ptest.startswith('test_') \
               and not ptest.startswith('test_failover_'):
                tests_to_run.append('Testlocal.%s' % ptest)

    unittest.main(defaultTest=tests_to_run, argv=uargs)
