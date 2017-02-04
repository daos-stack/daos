#!/usr/bin/env python3
# Copyright (c) 2016 Intel Corporation
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
# -*- coding: utf-8 -*-
"""
test runner post test run processing class

"""

import os
import stat
#pylint: disable=import-error
import GrindRunner
#pylint: enable=import-error

#pylint: disable=too-many-locals


class PostRunner(GrindRunner.GrindRunner):
    """post test runner"""
    last_testlogdir = ""
    logger = None

    def check_log_mode(self, topdir):
        """set the directory and file permissions"""
        mode = stat.S_IRWXU | stat.S_IRWXG | stat.S_IROTH | stat.S_IXOTH
        if os.path.exists(topdir):
            dirlist = os.listdir(topdir)
            for newdir in dirlist:
                dname = os.path.join(topdir, newdir)
                os.chmod(dname, mode)
                if os.path.isfile(dname):
                    continue
                else:
                    self.check_log_mode(dname)

    def dump_error_messages(self, testMethodName):
        """dump the ERROR tag from stdout file

        This first checks for a method-specific log directory and uses that
        if it exists, or if not then it dumps all subdirectories.

        To find a method specific subdirectory it splits the method on '_'
        and then checks the last segment, or all but the first segment.

        For a method called 'test_rpc_write' it would check for directories
        called 'write' or 'rpc_write' in that order.
        """
        parts = testMethodName.split('_', maxsplit=2)
        shortdir = None
        if len(parts) > 2:
            shortdir = os.path.join(self.last_testlogdir, parts[2])
        longdir = os.path.join(self.last_testlogdir, testMethodName[5:])
        if shortdir and os.path.exists(shortdir):
            self.top_logdir(shortdir, dumpLogs=True)
        elif os.path.exists(longdir):
            self.top_logdir(longdir, dumpLogs=True)
        else:
            self.logger.info("Directory not found: %s" % longdir)
            self.logger.info("Directory not found: %s" % shortdir)
            topdir = os.path.dirname(self.last_testlogdir)
            self.top_logdir(topdir, dumpLogs=True)

    def dump_logs(self, rankdir, filelist):
        """dump the ERROR tag from stdout file"""
        dumpstd = ""
        for stdfile in filelist:
            if stdfile == "stdout" or stdfile == "stderr":
                dumpstd = os.path.abspath(os.path.join(rankdir, stdfile))
            else:
                continue
            if os.path.exists(dumpstd):
                self.logger.info(
                    "****************************************************")
                self.logger.info("Error info from file\n %s" % dumpstd)
                filesize = os.path.getsize(dumpstd)
                if filesize > (12 * 1024):
                    self.logger.info(
                        "File too large (%d bytes), showing errors only" % \
                        filesize)
                    with open(dumpstd, 'r') as file:
                        for line in file:
                            if 'ERR' in line:
                                self.logger.info(line)
                elif filesize > 0:
                    with open(dumpstd, 'r') as file:
                        self.logger.info(file.read())
                else:
                    self.logger.info("--- empty ---\n")

    def testcase_logdir(self, rankdir, filelist):
        """walk the testcase directory and find stdout and stderr files"""
        dumpstdout = "No stdout file"
        dumpstderr = "No stderr file"
        for stdfile in filelist:
            if stdfile == "stdout":
                dumpstdout = os.path.abspath(os.path.join(rankdir, stdfile))
            elif stdfile == "stderr":
                dumpstderr = os.path.abspath(os.path.join(rankdir, stdfile))
        if dumpstdout or dumpstderr:
            self.logger.info("Log file %s\n %s\n %s" %
                             (rankdir, dumpstdout, dumpstderr))
            self.logger.info(
                "*****************************************************")

    def top_logdir(self, newdir, dumpLogs=False):
        """walk the testRun directory and find testcase directories"""
        for (dirpath, dirnames, filenames) in os.walk(newdir):
            self.logger.info(
                "*****************************************************")
            self.logger.info("directory: " + str(os.path.abspath(dirpath)))
            if filenames:
                self.logger.info("files: " + str(filenames))
            if not dirnames:
                if dumpLogs:
                    self.dump_logs(dirpath, filenames)
                else:
                    self.testcase_logdir(dirpath, filenames)
        self.logger.info(
            "*****************************************************")
