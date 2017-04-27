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

Fill in the Test section of the results.yml file From the provided information.

  Tests:
      name: group name
      status: PASS/FAIL
      submission: Tue Dec 01 16:36:46 PDT 2015
      duration: time

Next, from the location of the results file, start searching the
sub-directories for subtest_results.yml file. the contains of this file is
added to the SubTests list in the Tests section of the results file. For each
test listed find any logs files in the directory. A log file is any file with
the format of <test name>.[log,out,err]. if a log file is found it is copied to
root of the results directory and given a Maloo style name.

log format name:
    <group name>.<test name>[_<host name>].<log type>.<host name>.log

if the test are executed on more than one node, the node name is add to the
subtest name and to the test name of the log file. The host name if found from
the testing log directory created by test runner.

"""
import os
import stat
import re

#pylint: disable=too-many-locals


class PostRunner():
    """post test runner"""
    last_testlogdir = ""
    logger = None
    # save base directory
    results_base_dir = ""
    test_info = None

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

    def dump_log_files(self, testClassName, testMethodName):
        """dump the ERROR tag from stdout file

        This first checks for a method-specific log directory and uses that
        if it exists, or if not then it dumps all subdirectories.

        To find a method specific subdirectory it splits the method on '_'
        and then checks the last segment, or uses the testClassName and then
        all but the first segment of testMethodName

        For a class.method called 'MyClass.test_rpc_write' it would check for
        directories called 'write' or 'myClass/rpc_write' in that order.
        """
        parts = testMethodName.split('_', maxsplit=2)
        shortdir = None
        if len(parts) > 2:
            shortdir = os.path.join(self.last_testlogdir, parts[2])
        longdir = os.path.join(self.last_testlogdir,
                               testClassName,
                               testMethodName[5:])
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
            if 'rank' in stdfile:
                dumpstd = os.path.abspath(os.path.join(rankdir, stdfile))
            elif stdfile == "stdout" or stdfile == "stderr":
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
            if "stdout" in stdfile:
                dumpstdout = os.path.abspath(os.path.join(rankdir, stdfile))
            elif "stderr" in stdfile:
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
            if not dirnames and "rank" in dirpath:
                if dumpLogs:
                    self.dump_logs(dirpath, filenames)
                else:
                    self.testcase_logdir(dirpath, filenames)
        self.logger.info(
            "*****************************************************")

    @staticmethod
    def log_type(item):
        """copy in subtest results and find log files"""
        logtype = "."
        if len(item) < 2:
            return logtype
        extension = item[1]
        if extension == "log":
            logtype = "test_log"
        elif extension == "out":
            logtype = "console_out"
        elif extension == "err":
            logtype = "console_err"
        elif extension != "":
            logtype = "{!s}".format(item[1])
        return logtype

    def create_orte_log(self, subtest_results, rankdir, filelist):
        """walk the testcase directory and find stdout and stderr files"""
        dirparts = rankdir.split('/')
        rank = os.path.basename(rankdir).replace('.', '_')
        testname = ""
        # return name list
        found = re.search(r'loop(\d*)', rankdir)
        if found:
            self.logger.log(0, "LOOP: %s", found.group(1))
            loop = int(found.group(1))
        else:
            loop = 0
        test_set_name = subtest_results.test_set_name(loop=loop)
        testlist = subtest_results.get_subtest_list(loop=loop)
        if len(testlist) == 1:
            testname = "{!s}.{!s}_set_{!s}_{!s}".format(testlist[0]['name'],
                                                        dirparts[-3],
                                                        dirparts[-2],
                                                        rank)
        else:
            for test in testlist:
                self.logger.log(0, "TEST LIST: %s", str(test))
                if test['name'] in rankdir:
                    testname = "{!s}.{!s}_set_{!s}_{!s}".format(test['name'],
                                                                dirparts[-3],
                                                                dirparts[-2],
                                                                rank)
                    break
        self.logger.log(0, "TESTSETNAME: %s", test_set_name)
        self.logger.log(0, "TESTNAME: %s", testname)
        if testname:
            for stdfile in filelist:
                #pylint: disable=bad-continuation
                if stdfile == "stdout":
                    stdout = os.path.abspath(os.path.join(rankdir, stdfile))
                    new_name = os.path.abspath(os.path.join(rankdir,
                                            ("{!s}.{!s}_stdout_log.{!s}.log".
                                             format(test_set_name, testname,
                                                    self.test_info.nodeName())
                                            )))
                    os.rename(stdout, new_name)
                elif stdfile == "stderr":
                    stderr = os.path.abspath(os.path.join(rankdir, stdfile))
                    new_name = os.path.abspath(os.path.join(rankdir,
                                            ("{!s}.{!s}_stderr_log.{!s}.log".
                                             format(test_set_name, testname,
                                                    self.test_info.nodeName())
                                            )))
                    os.rename(stderr, new_name)
                #pylint: enable=bad-continuation

    def load_subtest_logs(self, subtest_results, dirpath, filelist):
        """walk the testcase directory and find out and err files"""
        dirparts = dirpath.split('/')
        testname = ""
        for filename in filelist:
            log_file = os.path.join(dirpath, filename)
            item = filename.split('.')
            if item[-1] == "runout" or item[-1] == "runerr":
                continue
            elif len(item) < 4:
                log_type = self.log_type(item)
                found = re.search(r'loop(\d*)', dirpath)
                if found:
                    self.logger.log(0, "LOOP: %s", found.group(1))
                    loop = int(found.group(1))
                else:
                    loop = 0
                test_set_name = subtest_results.test_set_name(loop=loop)
                testlist = subtest_results.get_subtest_list(loop=loop)
                if len(testlist) == 1:
                    testname = testlist[0]['name']
                else:
                    for test in testlist:
                        if test['name'] in dirpath:
                            testname = test['name']
                            break
                self.logger.log(0, "TESTSETNAME: %s", test_set_name)
                self.logger.log(0, "TESTNAME: %s", testname)
                typeplus = "{!s}_{!s}_{!s}".format(dirparts[-1],
                                                   item[0], log_type)

                new_name = ("{!s}.{!s}.{!s}.{!s}.log".
                            format(test_set_name, testname, typeplus,
                                   self.test_info.nodeName()))
                self.logger.log(0, "NEW_NAME: %a", new_name)
                new_path = os.path.join(dirpath, new_name)
                os.rename(log_file, new_path)

    def test_logdir(self, subtest_results):
        """walk the tests directory and find testcase directories"""
        test_base_dir = subtest_results.base_dir
        self.logger.log(0, "*********************************************")
        self.logger.log(0, "start here: %s", test_base_dir)
        for dirname in os.listdir(test_base_dir):
            test_dir = os.path.join(test_base_dir, dirname)
            self.logger.log(0,
                            "*********************************************")
            self.logger.log(0, "check this: %s", test_dir)
            if not os.path.isdir(test_dir):
                continue
            for (dirpath, dirnames, filenames) in os.walk(test_dir):
                self.logger.log(0,
                                "*********************************************")
                absdirpath = os.path.abspath(dirpath)
                self.logger.log(0, "directory log: %s", absdirpath)
                if not dirnames and "rank" in dirpath:
                    self.create_orte_log(subtest_results, absdirpath, filenames)
                elif filenames:
                    self.logger.log(0, "files: %s", filenames)
                    self.load_subtest_logs(subtest_results, absdirpath,
                                           filenames)
                elif not filenames and dirnames:
                    self.logger.log(0, "dir: %s", dirnames)
        self.logger.log(0, "*********************************************")

    def test_logtopdir(self, subtest_results):
        """find log files in the top log directory and rename if needed
           walk the tests sub-directory and find testcase"""
        filenames = []
        test_base_dir = subtest_results.base_dir
        self.logger.log(0, "*********************************************")
        self.logger.log(0, "start here: %s", test_base_dir)
        for dirname in os.listdir(test_base_dir):
            test_dir = os.path.join(test_base_dir, dirname)
            if os.path.isdir(test_dir):
                continue
            else:
                filenames.append(dirname)
        absdirpath = os.path.abspath(test_base_dir)
        self.load_subtest_logs(subtest_results, absdirpath, filenames)
        self.test_logdir(subtest_results)
