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
iof simple test: Simple test to stat the mount points.

Usage:

Executes from the install/Linux/TESTING directory.
The results are placed in the testLogs/testRun_<date-time-stamp>/
multi_test_nss/iof_simple/iof_simple_<node> directory.
"""

import os
import common_methods
import logging
import unittest
from stat import S_ISDIR

def setUpModule():
    """Set the global variables for the projection on CN"""

    startdir = os.environ["CNSS_PREFIX"]
    common_methods.CTRL_DIR = os.path.join(startdir, ".ctrl")
    common_methods.IMPORT_MNT = common_methods.get_writeable_import()

class TestIof(unittest.TestCase, common_methods.CnssChecks):
    """IOF filesystem tests in private access mode"""

    logger = logging.getLogger("TestRunnerLogger")
    test_local = False
    log_mask = ""
    crt_phy_addr = ""
    ofi_interface = ""
    cnss_prefix = ""

    @staticmethod
    def logdir_name():
        """create the log directory name"""
        return ""

    def setUp(self):
        """Set up the test"""

        # A unittest workaround to run the common_methods, both
        # iof_test_local and iof_simple. The former requires the variables
        # to be set up and torn down for each test. The latter requires the
        # variables to be set only once for each run.
        self.test_local = False
        self.import_dir = common_methods.IMPORT_MNT
        self.export_dir = self.import_dir
        self.log_mask = os.getenv("D_LOG_MASK", "INFO,CTRL=WARN")
        self.crt_phy_addr = os.getenv("CRT_PHY_ADDR_STR", "ofi+sockets")
        self.ofi_interface = os.getenv("OFI_INTERFACE", "eth0")
        self.cnss_prefix = os.getenv("CNSS_PREFIX")
        self.logger.info("\n")
        self.logger.info("*************************************************")
        self.logger.info("Starting for %s", self.id())
        self.logger.info("import directory %s", self.import_dir)

    def test_iof_fs(self):
        """Test private access mount points"""
        self.logger.info("starting to stat the mountpoints")
        entry = os.path.join(common_methods.CTRL_DIR, "iof", "projections")

        self.assertTrue(os.path.isdir(entry), \
            "Mount point %s not found" % entry)
        for projection in os.listdir(entry):
            myfile = os.path.join(entry, projection, 'mount_point')
            fd = open(myfile, "r")
            mnt_path = fd.readline().strip()
            self.logger.info("Mount path is %s", mnt_path)
            stat_obj = os.stat(mnt_path)

            self.logger.info(stat_obj)
            self.assertTrue(S_ISDIR(stat_obj.st_mode), "File type is \
                not a directory")

    def test_zzzz_theEnd(self):
        """mark the end"""
        self.logger.info("*************************************************")
        self.logger.info("\n\t\tWe are DONE\n")
        self.logger.info("*************************************************")
        self.logger.info("\n")
