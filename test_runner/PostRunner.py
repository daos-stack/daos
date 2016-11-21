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
        """dump the ERROR tag from stdout file"""
        mode = stat.S_IRWXU | stat.S_IRWXG | stat.S_IROTH | stat.S_IXOTH
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
        if  os.path.exists(newdir):
            self.dump_logs(newdir)
        else:
            self.logger.info("Directory not found: %s" % newdir)
            topdir = os.path.dirname(newdir)
            dirlist = sorted(os.listdir(topdir), reverse=True)
            for logdir in dirlist:
                self.dump_logs(os.path.join(topdir, logdir))

    def dump_logs(self, newdir):
        """dump the ERROR tag from stdout file"""
        if not os.path.exists(newdir):
            self.logger.info("Directory not found: %s" % newdir)
            return
        dirlist = sorted(os.listdir(newdir), reverse=True)
        for psdir in dirlist:
            dname = os.path.join(newdir, psdir)
            if os.path.isfile(dname):
                continue
            rankdirlist = sorted(os.listdir(dname), reverse=True)
            for rankdir in rankdirlist:
                dumpstdout = os.path.join(newdir, psdir, rankdir, "stdout")
                dumpstderr = os.path.join(newdir, psdir, rankdir, "stderr")
                self.logger.info(
                    "****************************************************")
                self.logger.info("Error info from file\n %s" % dumpstdout)
                filesize = os.path.getsize(dumpstdout) > 1024
                if filesize > 1024:
                    self.logger.info(
                        "File too large (%d bytes), showing errors only" % \
                        filesize)
                    with open(dumpstdout, 'r') as file:
                        for line in file:
                            if line.startswith("ERROR:"):
                                self.logger.info(line)
                else:
                    with open(dumpstdout, 'r') as file:
                        self.logger.info(file.read())

                self.logger.info("Error info from file\n %s" % dumpstderr)
                if os.path.getsize(dumpstderr) > 0:
                    with open(dumpstderr, 'r') as file:
                        self.logger.info(file.read())
                else:
                    self.logger.info("--- empty ---\n")

    def testcase_logdir(self, logdir):
        """walk the testcase directory and find stdout and stderr files"""
        # testcase directories
        dirlist = sorted(os.listdir(logdir), reverse=True)
        for psdir in dirlist:
            dname = os.path.join(logdir, psdir)
            if os.path.isfile(dname):
                continue
            rankdirlist = sorted(os.listdir(dname), reverse=True)
            # rank directories
            for rankdir in rankdirlist:
                if os.path.isfile(os.path.join(logdir, psdir, rankdir)):
                    continue
                dumpstdout = os.path.join(logdir, psdir, rankdir, "stdout")
                dumpstderr = os.path.join(logdir, psdir, rankdir, "stderr")
                self.logger.info(
                    "*****************************************************")
                self.logger.info("Log file %s\n %s\n %s" %
                                 (rankdir, dumpstdout, dumpstderr))
                self.logger.info(
                    "*****************************************************")

    def top_logdir(self, newdir, dumpLogs=False):
        """walk the testRun directory and find testcase directories"""
        # testRun directory
        dirlist = sorted(os.listdir(newdir), reverse=True)
        # test loop directories
        for loopdir in dirlist:
            loopname = os.path.join(newdir, loopdir)
            if os.path.isfile(loopname):
                continue
            self.logger.info(
                "*****************************************************")
            self.logger.info("%s" % loopdir)
            iddirlist = sorted(os.listdir(loopname), reverse=True)
            # test id directories
            for iddir in iddirlist:
                idname = os.path.join(loopname, iddir)
                if os.path.isfile(idname):
                    continue
                self.logger.info(
                    "*****************************************************")
                self.logger.info("%s" % iddir)
                tcdirlist = sorted(os.listdir(idname), reverse=True)
                for tcdir in tcdirlist:
                    tcname = os.path.join(idname, tcdir)
                    if os.path.isfile(tcname):
                        continue
                    self.logger.info(
                        "*****************************************************")
                    self.logger.info("%s" % tcdir)
                    if dumpLogs:
                        self.dump_logs(tcname)
                    else:
                        self.testcase_logdir(tcname)
