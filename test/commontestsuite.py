#!/usr/bin/env python3
# Copyright (C) 2016-2018 Intel Corporation
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
# -*- coding: utf-8 -*-
"""
common setUp, teardown and other modules for the testsuites.

Usage:

The common module is imported and executed by the cart test suites.
The module contains the supporting functions for the tests:
setUp, tearDown, create the input/command string and set the log dir.

The results will be placed in the testLogs/testRun directory. There
you will find anything written to stdout and stderr. The output from memcheck
and callgrind are in the test_group directory. At the end of a test run, the
last testRun directory is renamed to testRun_<date stamp>

To use valgrind memory checking
set TR_USE_VALGRIND in cart_echo_test.yml to memcheck

To use valgrind call (callgrind) profiling
set TR_USE_VALGRIND in cart_echo_test.yml to callgrind

To redirect output to the log file,
set TR_REDIRECT_OUTPUT in the respective yaml files.
The test_runner will set it to redirect the output to the log file.
By default the output is displayed on the screen.

"""

import os
import unittest
import subprocess
import shlex
import time
import getpass
import logging

#pylint: disable=broad-except
#pylint: disable=too-many-locals

class CommonTestSuite(unittest.TestCase):
    """Contains the attributes common to the CART tests"""
    logger = logging.getLogger("TestRunnerLogger")

    testprocess = ""
    testsuite = ""
    testTitle = ""
    reservedSocket = None

    def get_test_info(self):
        """Retrieve the information from test id"""
        self.logger.info("test name: %s", self.id())
        test_id = self.id()
        (self.testprocess, self.testsuite, self.testTitle) = test_id.split(".")

    def launch_test(self, testdesc, NPROC, env, **kwargs):
        """Method creates the Client or Client and Server arguments
           depending on the test being two_node or one_node respectively.
           Apart from the positional parameters, the method is invoked with
           the respective test instance and its related arguments. The
           processes will be launched in the foreground."""
        cmdstr = ""
        server = ""

        # Unpack kwargs; Read the client, server; and resepctive args
        cli_arg = kwargs.get('cli_arg')
        srvr_arg = kwargs.get('srv_arg')
        client = kwargs.get('cli', "")

        (cmd, prefix) = self.add_prefix_logdir()
        cli_cmdstr = "{!s} -N {!s} {!s}{!s} {!s}".format(
            client, NPROC, env, prefix, cli_arg)

        # The one node test passes both Client and Server args.
        # Otherwise passes only Client args.
        if not srvr_arg:
            # Launch the client in the foreground
            cmdstr = cmd + cli_cmdstr
        else:
            srv_cmdstr = " {!s} -N {!s} {!s}{!s} {!s} :".format(
                server, NPROC, env, prefix, srvr_arg)
            # Launch server and client on the same node
            cmdstr = cmd + srv_cmdstr + cli_cmdstr

        procrtn = self.execute_cmd(testdesc, cmdstr)
        return procrtn

    def launch_bg(self, testdesc, NPROC, env, *args):
        """Launch the server in the background"""
        server, srvr_arg = args

        # Create the input string with server args
        (cmd, prefix) = self.add_prefix_logdir()
        cmdstr = "{!s} {!s} -N {!s} {!s}{!s} {!s}".format(
            cmd, server, NPROC, env, prefix, srvr_arg)

        procrtn = self.call_process(testdesc, cmdstr)
        return procrtn

    def execute_cmd(self, msg, cmdstr):
        """Launch process set test"""
        self.logger.info("%s: start %s - input string:\n%s", \
            self.testsuite, msg, cmdstr)
        cmdarg = shlex.split(cmdstr)
        start_time = time.time()
        to_redirect = os.getenv('TR_REDIRECT_OUTPUT', "no").lower()

        # If TR_REDIRECT_OUTPUT = "no"; the output is redirected to the screen
        # else if TR_REDIRECT_OUTPUT = "null"; the ouput goes to devnull
        # else if TR_REDIRECT_OUTPUT = "yes"; the output goes to a file.
        if to_redirect == "no":
            procrtn = subprocess.call(cmdarg, timeout=180)
        elif to_redirect == "null":
            procrtn = subprocess.call(cmdarg, timeout=180,
                                      stdout=subprocess.DEVNULL,
                                      stderr=subprocess.DEVNULL)
        else:
            log_path = os.path.join(os.getenv("CRT_TESTLOG", "output"))
            if not os.path.exists(log_path):
                os.makedirs(log_path)
            cmdfileout = os.path.join(log_path, "common_launch_test.out")
            cmdfileerr = os.path.join(log_path, "common_launch_test.err")
            with open(cmdfileout, mode='a') as outfile, \
                open(cmdfileerr, mode='a') as errfile:
                outfile.write("{!s}\n  Command: {!s} \n{!s}\n".format(
                    ("=" * 40), cmdstr, ("=" * 40)))
                outfile.flush()
                errfile.write("{!s}\n  Command: {!s} \n{!s}\n".format(
                    ("=" * 40), cmdstr, ("=" * 40)))
                procrtn = subprocess.call(cmdarg, timeout=180,
                                          stdout=outfile,
                                          stderr=errfile)
        elapsed = time.time() - start_time
        self.logger.info("%s: %s - return code: %d test duration: %d\n", \
          self.testsuite, msg, procrtn, elapsed)
        return procrtn

    def call_process(self, msg, cmdstr):
        """Launch process set """
        self.logger.info("%s: start process %s - input string:\n%s", \
          self.testsuite, msg, cmdstr)
        cmdarg = shlex.split(cmdstr)
        to_redirect = os.getenv('TR_REDIRECT_OUTPUT', "no").lower()

        # If TR_REDIRECT_OUTPUT = "no"; the output is redirected to the screen
        # else if TR_REDIRECT_OUTPUT = "null"; the ouput goes to devnull
        # else if TR_REDIRECT_OUTPUT = "yes"; the output goes to a file.
        if to_redirect == "no":
            proc = subprocess.Popen(cmdarg)
        elif to_redirect == "null":
            proc = subprocess.Popen(cmdarg,
                                    stdout=subprocess.DEVNULL,
                                    stderr=subprocess.DEVNULL)
        else:
            log_path = os.path.join(os.getenv("CRT_TESTLOG", "output"))
            if not os.path.exists(log_path):
                os.makedirs(log_path)
            cmdfileout = os.path.join(log_path, "common_launch_process.out")
            cmdfileerr = os.path.join(log_path, "common_launch_process.err")
            with open(cmdfileout, mode='a') as outfile, \
                open(cmdfileerr, mode='a') as errfile:
                outfile.write("{!s}\n  Command: {!s} \n{!s}\n".format(
                    ("=" * 40), cmdstr, ("=" * 40)))
                outfile.flush()
                errfile.write("{!s}\n  Command: {!s} \n{!s}\n".format(
                    ("=" * 40), cmdstr, ("=" * 40)))
                proc = subprocess.Popen(cmdarg,
                                        stdout=outfile,
                                        stderr=errfile)
        return proc


    def check_process(self, proc):
        """Check if a process is still running"""
        proc.poll()
        procrtn = proc.returncode
        if procrtn is None:
            return True
        self.logger.info("Process has exited")
        return False

    def stop_process(self, msg, proc):
        """ wait for process to terminate """
        self.logger.info("%s: %s - stopping processes :%s", \
          self.testsuite, msg, proc.pid)
        i = 60
        procrtn = None
        while i:
            proc.poll()
            procrtn = proc.returncode
            if procrtn is not None:
                break
            else:
                time.sleep(1)
                i = i - 1

        if procrtn is None:
            self.logger.info("%s: Again stopping processes :%s", \
              self.testsuite, proc.pid)
            procrtn = -1
            try:
                proc.terminate()
                proc.wait(2)
            except ProcessLookupError:
                pass
            except Exception:
                self.logger.info("%s: killing processes :%s", \
                  self.testsuite, proc.pid)
                proc.kill()

        self.logger.info("%s: %s - return code: %d\n", \
          self.testsuite, msg, procrtn)

        return procrtn

    @staticmethod
    def logdir_name(fullname):
        """create the log directory name"""
        names = fullname.split('.')
        items = names[-1].split('_', maxsplit=2)
        return items[2]

    def get_cart_long_log_path(self):
        """get the long format log path"""
        testcase_id = self.id()
        log_path = os.path.join(os.getenv("CRT_TESTLOG", self.testprocess),
                                self.logdir_name(testcase_id))
        return log_path

    def get_cart_long_log_name(self):
        """get cart log file name with the long format log path"""
        long_log_name = os.path.join(self.get_cart_long_log_path(),
                                     'output.log')

        return long_log_name

    def add_prefix_logdir(self):
        """add the log directory to the prefix"""
        testcase_id = self.id()
        prefix = ""
        ompi_bin = os.getenv('CRT_OMPI_BIN', "")
        log_path = os.path.join(os.getenv("CRT_TESTLOG", self.testprocess),
                                self.logdir_name(testcase_id))
        os.makedirs(log_path, exist_ok=True)
        use_valgrind = os.getenv('TR_USE_VALGRIND', default="")
        if use_valgrind == 'memcheck':
            suppressfile = os.path.join(os.getenv('CRT_PREFIX', ".."), "etc", \
                           "memcheck-cart.supp")
            prefix = " valgrind --xml=yes" + \
                " --xml-file=" + log_path + "/valgrind.%q{PMIX_ID}.memcheck" + \
                " --fair-sched=try " + \
                " --partial-loads-ok=yes" + \
                " --leak-check=yes --gen-suppressions=all" + \
                " --suppressions=" + suppressfile + " --show-reachable=yes"
        elif use_valgrind == "callgrind":
            prefix = " valgrind --tool=callgrind --callgrind-out-file=" + \
                     log_path + "/callgrind.%q{PMIX_ID}.out"

        if os.getenv('TR_USE_URI', ""):
            dvmfile = " --ompi-server file:%s " % os.getenv('TR_USE_URI')
        else:
            dvmfile = " "
        if getpass.getuser() == "root":
            allow_root = " --allow-run-as-root"
        else:
            allow_root = ""
        cmdstr = "%sorterun --mca btl self,tcp %s--output-filename %s%s" % \
                 (ompi_bin, dvmfile, log_path, allow_root)

        return (cmdstr, prefix)

    @staticmethod
    def get_server_list():
        """add prefix to each server and return the list of servers"""
        server_list = []
        servers = os.getenv('CRT_TEST_SERVER')
        if servers:
            server_list = servers.split(',')

        return server_list

    @staticmethod
    def get_client_list():
        """add prefix to each client and return the list of clients"""
        client_list = []
        clients = os.getenv('CRT_TEST_CLIENT')
        if clients:
            client_list = clients.split(',')

        return client_list
