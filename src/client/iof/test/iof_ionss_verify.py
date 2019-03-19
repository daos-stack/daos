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
Test methods to verify the outcome of the tests that were run on the CN.
These tests are run on the ION.

These methods are invoked by the multi-instance test launch after the
iof_simple_test.

"""

import os
import logging
import unittest
import json
from decimal import Decimal
from socket import gethostname

#pylint: disable=no-member


class IonssVerify():
    """A object purely to verify the outcome of the tests carried out
    on the CN."""
    logger = logging.getLogger("TestRunnerLogger")
    export_dir = ""

    def verify_clean_up_ionss_link(self):
        """Clean up the link created to test"""

        self.logger.info("verify_clean_up_ionss_link %s", self.export_dir)
        os.unlink(os.path.join(self.export_dir, 'a'))
        os.rmdir(os.path.join(self.export_dir, 'b'))

    def verify_clean_up_ionss_self_listdir(self):
        """Clean up after the list dir"""

        test_dir = os.path.join(self.export_dir, 'test_dir')
        self.logger.info("verify_clean_up_ionss_self_listdir %s", test_dir)
        os.rmdir(test_dir)

    def verify_file_copy(self):
        """Verify the file has been copied on the projection"""

        filename = os.path.join(self.export_dir, 'ls')
        self.logger.info("verify_file_copy %s", filename)

        if not os.path.isfile(filename) or not os.access(filename, os.R_OK):
            self.fail("Failed to copy file into the projection")
        else:
            self.logger.info("Copied file exists in the projection")

        os.unlink(filename)

    def verify_file_copy_from(self):
        """Verify the copy of the file made into the projection"""

        filename = os.path.join(self.export_dir, 'ls.2')
        self.logger.info("verify_file_copy_from %s", filename)

        if not os.path.isfile(filename) or not os.access(filename, os.R_OK):
            self.fail("Failed to copy file into the projection")
        else:
            self.logger.info("Copied file exists in the projection")

        os.unlink(filename)

    def verify_file_rename(self):
        """Verify the contents of the renamed file"""

        filename = os.path.join(self.export_dir, 'd_file')
        self.logger.info("verify_file_rename %s", filename)

        fd = open(filename, 'r')
        data = fd.read()
        fd.close()

        if data != 'World':
            self.fail('File contents wrong %s %s' % ('Hello', data))
        else:
            self.logger.info("Contents from the renamed file: %s %s", \
                              'Hello', data)

        os.unlink(filename)

    def verify_file_write(self):
        """Verify the file written on the projected FS"""

        filename = os.path.join(self.export_dir, 'write_file')
        self.logger.info("verify_file_write %s", filename)

        fd = open(filename, 'r')
        data = fd.read()
        fd.close()

        if data != 'World':
            self.fail('File contents wrong %s %s' % ('Hello', data))
        else:
            self.logger.info("Contents from file written to: %s %s", \
                              'Hello', data)

        os.unlink(filename)

    def verify_file_unlink(self):
        """Verify the file has been removed"""

        filename = os.path.join(self.export_dir, 'unlink_file')
        self.logger.info("verify_file_unlink %s", filename)
        if os.path.exists(filename):
            self.fail("File unlink failed.")
        else:
            self.logger.info("File unlinked.")

    def verify_make_symlink(self):
        """Verify the symlink created on the projection"""

        self.logger.info("List the files on ION")
        self.logger.info(os.listdir(self.export_dir))
        filename = os.path.join(self.export_dir, 'mlink_source')
        self.logger.info("verify_make_symlink %s", filename)

        self.logger.info(os.lstat(filename))
        result = os.readlink(filename)
        if result != 'mlink_target':
            self.fail("Link target is wrong '%s'" % result)
        else:
            self.logger.info("Verified link target with source.")

        os.unlink(filename)

    def verify_many_files(self):
        """Verify the collection of files created"""

        many_dir = os.path.join(self.export_dir, 'many')
        self.logger.info("verify_many_files %s", many_dir)

        export_list = os.listdir(many_dir)
        self.logger.info("verify_many_files export_list %s",
                         sorted(export_list))

        with open(os.path.join(self.export_dir, 'file_list'), 'r') as f:
            files = [line.rstrip('\n') for line in f]

        self.logger.info("verify_many_files file_list %s", sorted(files))

        if sorted(files) != sorted(export_list):
            self.fail("Export Directory contents are wrong")
        else:
            self.logger.info("Import and Export directory contents match")

    def verify_rmdir(self):
        """Verify the dir has been removed"""

        my_dir = os.path.join(self.export_dir, 'my_dir')
        self.logger.info("verify_rmdir %s", my_dir)
        if os.path.exists(my_dir):
            self.fail("Directory has been removed.")
        else:
            self.logger.info("Directory removed.")

    @staticmethod
    def file_length(fname):
        """ Return the number of lines in fname"""
        num_lines = sum(1 for line in open(fname))
        return num_lines

    def verify_use_ino(self):
        """Compare and verify the stat results on a file from CN and ION"""

        filename = os.path.join(self.export_dir, 'test_ino_file')
        self.logger.info("verify_use_ino %s", filename)
        ion_stats = os.stat(filename)
        os.unlink(filename)

        # Currently the FUSE plugin does not correctly report inodes
        # so currently there are differences.

        diffs = []
        # Compare the stat values recorded on the CN and ION
        cn_stat_file = os.path.join(self.export_dir, 'cn_stat_output')
        with open(cn_stat_file, 'r') as fd:
            cn_stats = json.load(fd)

        for key in dir(ion_stats):
            if not key.startswith('st_'):
                continue
            elif key == 'st_dev':
                continue
            ionv = str(Decimal(getattr(ion_stats, key)))
            cnv = cn_stats.get(key, "")
            self.logger.error("Key %s ion: %s cn: %s",
                              key, ionv, cnv)

            if cnv != ionv:
                self.logger.error("Keys %s are differnet ion: %s cn: %s",
                                  key, ionv, cnv)
                diffs.append(key)

        if diffs:
            self.fail("Stat attributes are different %s" % diffs)

        os.unlink(cn_stat_file)

    def verify_zzzz_theEnd(self):
        """mark the end"""
        self.logger.info("*************************************************")
        self.logger.info("\n\t\tWe are verified\n")
        self.logger.info("*************************************************")


class IonssCheckVerify(unittest.TestCase, IonssVerify):
    """A object purely to verify the outcome of the tests carried out
    on the CN.
    These methods are invoked on the ION from where the files system
    will be projected/exported to the CNSS.

    This class imports from unittest to get access to the self.fail() method
    """
    logger = logging.getLogger("TestRunnerLogger")
    export_dir = ""

    def setUp(self):
        """Set up the export dir"""

        self.ion = os.environ["IOF_TEST_ION"].split(',')
        curr_host = gethostname().split('.')[0]
        if curr_host != self.ion[0]:
            self.skipTest('The current ION list containts more than one node.\
                           The tests cannot run on more that one ION.')

        self.e_dir = os.environ["ION_TEMPDIR"]
        self.export_dir = os.path.join(self.e_dir, 'exp')
        #for filename in os.listdir(self.export_dir):
        #    if filename.startswith("wolf"):
        #        self.export_dir = os.path.join(self.export_dir, filename)
        #    else:
        #        continue

        self.logger.info("\n")
        self.logger.info("*************************************************")
        self.logger.info("Starting for %s", self.id())
