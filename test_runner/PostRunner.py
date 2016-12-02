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
test runner class

"""

import os
import subprocess
import stat

#pylint: disable=too-many-locals


class PostRunner():
    """post test runner"""
    last_testlogdir = ""
    test_info = {}
    info = None
    logger = None

    def valgrind_memcheck(self):
        """ If memcheck is used to check the results """
        self.logger.info("TestRunner: valgrind memcheck begin")
        from xml.etree import ElementTree
        rtn = 0
        error_str = ""
        newdir = self.last_testlogdir
        if not os.path.exists(newdir):
            self.logger.info("Directory not found: %s" % newdir)
            return
        dirlist = os.listdir(newdir)
        for psdir in dirlist:
            dname = os.path.join(newdir, psdir)
            if os.path.isfile(dname):
                continue
            testdirlist = os.listdir(dname)
            for fname in testdirlist:
                if str(fname).endswith(".xml"):
                    with open(os.path.join(dname, fname), "r") as xmlfile:
                        tree = ElementTree.parse(xmlfile)
                    error_types = {}
                    for node in tree.iter('error'):
                        kind = node.find('./kind')
                        if not kind.text in error_types:
                            error_types[kind.text] = 0
                        error_types[kind.text] += 1
                    if error_types:
                        error_str += "test dir: %s  file: %s\n" % (psdir, fname)
                        for err in error_types:
                            error_str += "%-3d %s errors\n"%(error_types[err],
                                                             err)
        if error_str:
            self.logger.info("""
#########################################################
    memcheck TESTS failed.
%s
#########################################################
""" % error_str)
            rtn = 1
        self.logger.info("TestRunner: valgrind memcheck end")
        return rtn

    def callgrind_annotate(self):
        """ If callgrind is used pull in the results """
        self.logger.info("TestRunner: Callgrind annotate begin")
        module = self.test_info['module']
        srcdir = module.get('srcDir', "")
        src_rootdir = self.info.get_info('SRCDIR')
        if srcdir and src_rootdir:
            if isinstance(srcdir, str):
                srcfile = " %s/%s/*.c" % (src_rootdir, srcdir)
            else:
                srcfile = ""
                for item in srcdir:
                    srcfile += " %s/%s/*.c" % (src_rootdir, item)
            dirlist = os.listdir(self.last_testlogdir)
            for infile in dirlist:
                if os.path.isfile(infile) and infile.find(".out"):
                    outfile = infile.replace("out", "gp.out")
                    cmdarg = "callgrind_annotate " + infile + srcfile
                    self.logger.info(cmdarg)
                    with open(outfile, 'w') as out:
                        subprocess.call(cmdarg, timeout=30, shell=True,
                                        stdout=out,
                                        stderr=subprocess.STDOUT)
        else:
            self.logger.info("TestRunner: Callgrind no source directory")
        self.logger.info("TestRunner: Callgrind annotate end")

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
        """dump the ERROR tag from stdout file"""
        dirname = testMethodName.split('_', maxsplit=2)
        newdir = os.path.join(self.last_testlogdir, dirname[2])
        if os.path.exists(newdir):
            self.top_logdir(newdir, dumpLogs=True)
        else:
            self.logger.info("Directory not found: %s" % newdir)
            topdir = os.path.dirname(newdir)
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
                if filesize > 1024:
                    self.logger.info(
                        "File too large (%d bytes), showing errors only" % \
                        filesize)
                    with open(dumpstd, 'r') as file:
                        for line in file:
                            if line.startswith("ERROR:"):
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
