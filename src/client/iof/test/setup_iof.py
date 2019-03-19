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
setup_iof is invoked by the multi instance test runner and runs prior
to the iof_simple.
The setup_iof waits 10s for the ctrl_fs to start and validates the existence
of the same.

Usage:

Executes from the install/Linux/TESTING directory.
The results are placed in the testLogs/testRun_<date-time-stamp>/
multi_test_nss/setup_iof/setup_iof_<node> directory.
"""

#import unittest
import os
import time
import logging
from stat import S_ISREG

#class TestSetUpIof(unittest.TestCase):
class TestSetUpIof():
    """Set up and start ctrl fs"""

    def __init__(self, test_info=None, log_base_path=None):
        self.test_info = test_info
        self.log_dir_base = log_base_path
        self.logger = logging.getLogger("TestRunnerLogger")

    def useLogDir(self, log_path):
        """create the log directory name"""
        self.log_dir_base = log_path

    def test_iof_started(self):
        """Wait for ctrl fs to start"""
        start_dir = self.test_info.get_defaultENV("CNSS_PREFIX")
        self.logger.info("start_dir: %s", str(os.listdir(start_dir)))
        ctrl_dir = os.path.join(start_dir, ".ctrl")
        assert os.path.isdir(start_dir), \
               "prefix is not a directory %s" % start_dir
        filename = os.path.join(ctrl_dir, 'active')
        i = 10
        while i > 0:
            i = i - 1
            time.sleep(1)
            if not os.path.exists(filename):
                continue
            self.logger.info("Found active file: %s", filename)

            stat_obj = os.stat(filename)
            assert S_ISREG(stat_obj.st_mode), "File type is not a regular file"
            self.logger.info(stat_obj)

            fd = open(filename)
            data = fd.read()
            fd.close()
            if data.strip() == '1':
                return 0

        self.logger.info("start_dir: %s", str(os.listdir(start_dir)))
        # Log the error message. Fail the test with the same error message
        self.logger.info("Unable to detect file: %s", filename)
        return 1
