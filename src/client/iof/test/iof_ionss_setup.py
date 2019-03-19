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
Test methods for creating the test environment on the ION for the tests
in the common_methods whihc test the filesystem projected on the CN.

These methods are invoked by the multi-instance test launch before the
iof_simple_test.

"""

import os
import logging
import unittest
import shutil

class IonssExport():
    """These methods are for setting the files system that
    will be projected/exported to the CNSS.
    Note: all unittest methods start with export_ """
    logger = logging.getLogger("TestRunnerLogger")
    export_dir = None

    def export_file_copy_from(self):
        """Copy a file into a projecton"""

        src_file = os.path.join(self.export_dir, 'ls')
        self.logger.info("export_file_copy_from %s", src_file)
        shutil.copyfile('/bin/ls', src_file)

        dst_file = os.path.join(self.export_dir, 'ls.2')
        fd = open(dst_file, 'w+')
        fd.close()

    def export_file_open_existing(self):
        """Export a file to test test_file_open_existing"""

        tfile = os.path.join(self.export_dir, 'exist_file')
        self.logger.info("export_file_open_existing %s", tfile)

        fd = open(tfile, 'w')
        fd.write("Hello")
        fd.close()

    def export_file_read(self):
        """Export a file to test test_file_read"""

        tfile = os.path.join(self.export_dir, 'read_file')
        self.logger.info("export_file_read %s", tfile)

        fd = open(tfile, 'w')
        fd.write("Hello")
        fd.close()

    def export_ionss_link(self):
        """Create a symlink to check the type returned by stat"""
        # Make a directory 'b', create a symlink from 'a' to 'b'

        self.logger.info("export_ionss_link %s", self.export_dir)
        os.mkdir(os.path.join(self.export_dir, 'b'))
        os.symlink('b', os.path.join(self.export_dir, 'a'))

    def export_ionss_self_listdir(self):
        """For list dir operation, create a dir on io node"""

        test_dir = os.path.join(self.export_dir, 'test_dir')
        self.logger.info("export_ionss_self_listdir %s", test_dir)
        os.mkdir(test_dir)

    def export_many_files(self):
        """Create a dir to contain the files for test_many_files"""

        many_dir = os.path.join(self.export_dir, 'many')
        self.logger.info("export_many_files %s", many_dir)
        os.mkdir(many_dir)

    def export_read_symlink(self):
        """Create a symlink to be read by the CN"""

        rlink_source = os.path.join(self.export_dir, 'rlink_source')
        self.logger.info("export_read_symlink %s", rlink_source)
        os.symlink('rlink_target', rlink_source)

    def export_zzzz_theEnd(self):
        """mark the end"""
        self.logger.info("*************************************************")
        self.logger.info("\n\t\tWe are DONE\n")
        self.logger.info("*************************************************")


class IonssChecksSetup(unittest.TestCase, IonssExport):

    """These methods are invoked on the ION from where the files system
    will be projected/exported to the CNSS.

    These will provide a side-channel to the exports.

    Additionally, for multi-node these tests are executed in parallel so need
    to be carefull to use unique filenames and should endevour to clean up
    after themselves properly.

    This class imports from unittest to get access to the self.fail() method
    """
    def setUp(self):
        """Set up the export dir"""
        self.export_dir = os.path.join(os.environ["ION_TEMPDIR"], 'exp')
        self.logger.info("*************************************************")
        self.logger.info("Starting for %s", self.id())
