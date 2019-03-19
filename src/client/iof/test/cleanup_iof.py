#!/usr/bin/env python3
# Copyright (C) 2016-2017 Intel Corporation
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
iof cleanup: Terminate the processes and verify the same.

Usage:

Executes from the install/Linux/TESTING directory.
The results are placed in the testLogs/testRun_<date-time-stamp>/
multi_test_nss/cleanup_iof/cleanup_iof_<node> directory.
"""

import os
import time
import logging
import shlex
import subprocess

class TestCleanUpIof():
    """Set up and start ctrl fs"""

    def __init__(self, test_info=None, log_base_path=None):
        self.test_info = test_info
        self.log_dir_base = log_base_path
        self.logger = logging.getLogger("TestRunnerLogger")
        self.startdir = self.test_info.get_defaultENV("CNSS_PREFIX")
        self.ctrl_dir = os.path.join(self.startdir, ".ctrl")

    def useLogDir(self, log_path):
        """create the log directory name"""
        self.log_dir_base = log_path

    def launch_cmd(self, msg, cmdstr):
        """Launch a test"""
        self.logger.info("Testnss: start %s - input string:\n %s\n", \
          msg, cmdstr)
        cmdarg = shlex.split(cmdstr)
        procrtn = subprocess.call(cmdarg, timeout=180)
        return procrtn

    def mark_log(self, msg):
        """Log a message to stdout and to the CNSS logs"""

        log_file = os.path.join(self.ctrl_dir, 'write_log')
        with open(log_file, 'w')  as fd:
            fd.write(msg)

    def has_terminated(self, proc, waittime):
        """Check if the process has terminated
        Wait up to waittime for the process to die by itself,
        return True if process has exited, or false if it
        is still running"""
        i = waittime
        while i > 0:
            msg = "Check if the %s process has terminated" % proc
            cmd = "pgrep -la %s" % proc
            procrtn = self.launch_cmd(msg, cmd)
            if procrtn is 1:
                return True
            time.sleep(1)
            i = i - 1
        return False

    def test_iofshutdown(self):
        """Shutdown iof"""
        rtn = 1
        nodetype = self.test_info.info.get_config('node_type', '', "")
        if nodetype == "IOF_TEST_CN":
            rtn = self.cnss_shutdown()
        elif nodetype == "IOF_TEST_ION":
            rtn = self.ionss_shutdown()
        else:
            self.logger.info("IOF Cleanup failed. \
                        No node types match found \
                        node type: %s.\n", \
                        nodetype)
        return rtn

    def ionss_shutdown(self):
        """Shutdown iof"""
        self.logger.info("wait for ionss shutdown")
        start_time = time.time()
        ionssrtn = self.has_terminated("ionss", 20)
        elapsed = time.time() - start_time
        if ionssrtn:
            self.logger.info("CNSS and IONSS processes have terminated in "
                             "%d seconds\n",
                             elapsed)
        else:
            # Log the error message. Fail the test with the same error message
            self.logger.info("IOF Cleanup failed. IONSS processes have not "
                             "terminated in %d seconds.",
                             elapsed)
            return 1
        return 0

    def cnss_shutdown(self):
        """Shutdown iof"""
        shutdown_file = os.path.join(self.ctrl_dir, "shutdown")
        self.logger.info("Check for shutdown file: %s", shutdown_file)
        self.mark_log("Shutting down from cleanup_iof.py")
        start_time = time.time()
        with open(shutdown_file, 'w') as f:
            f.write('1')
        cnssrtn = self.has_terminated("cnss", 10)
        elapsed = time.time() - start_time
        if cnssrtn:
            self.logger.info("CNSS processes have terminated in %d seconds.\n",
                             elapsed)
        else:
            # Log the error message. Fail the test with the same error message
            self.logger.info("IOF Cleanup failed. CNSS processes have not "
                             "terminated in %d seconds",
                             elapsed)
            return 1
        return 0
