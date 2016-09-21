#!/usr/bin/env python3
# Copyright (C) 2016 Intel Corporation
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
mcl processs set test

Usage:

Execute from the install/$arch/TESTING directory. The results are placed in the
testLogs/testRun/echo_test directory. Any echo_test output is under
<file yaml>_loop#/<module.name.execStrategy.id>/1(process set)/rank<number>.
There you will find anything written to stdout and stderr. The output from
memcheck and callgrind are in the echo_test directory. At the end of a test run,
the last testRun directory is renamed to testRun_<date stamp>

python3 test_runner srcipts/mcl_echo_test.yml

To use valgrind memory checking
set TR_USE_VALGRIND in mcl_echo_test.yml to memcheck

To use valgrind call (callgrind) profiling
set TR_USE_VALGRIND in mcl_echo_test.yml to callgrind

"""

import os
import unittest
import subprocess
import shlex
import time
import getpass

#pylint: disable=broad-except

NPROC = "4"

def setUpModule():
    """ set up test environment """

    print("\nTestEcho: module setup begin")
    ompi_lib_path = os.path.join(os.getenv('CRT_OMPI_PREFIX', ".."), "lib")
    cart_lib_path = os.path.join(os.getenv('CRT_PREFIX', ".."), "lib")
    ld_lib_path = ompi_lib_path  + ":" + cart_lib_path
    os.environ["LD_LIBRARY_PATH"] = ld_lib_path
    print("TestEcho: module setup end\n\n")

def tearDownModule():
    """teardown module for test"""
    print("TestEcho: module tearDown begin")
    testmsg = "terminate any echo_test processes"
    cmdstr = "pkill crt_echo"
    launch_test(testmsg, cmdstr)
    print("TestEcho: module tearDown end\n\n")

def launch_test(msg, cmdstr):
    """Launch process set test"""
    print("TestEcho: start %s - input string:\n %s\n" % (msg, cmdstr))
    cmdarg = shlex.split(cmdstr)
    start_time = time.time()
    procrtn = subprocess.call(cmdarg, timeout=180,
                              stdout=subprocess.DEVNULL,
                              stderr=subprocess.DEVNULL)
    elapsed = time.time() - start_time
    print("TestEcho: %s - return code: %d test duration: %d\n" %
          (msg, procrtn, elapsed))
    return procrtn

def launch_process(msg, cmdstr):
    """Launch process set """
    print("TestEcho: start process %s - input string:\n %s\n" % (msg, cmdstr))
    cmdarg = shlex.split(cmdstr)
    proc = subprocess.Popen(cmdarg,
                            stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL)
    return proc

def stop_process(msg, proc):
    """ wait for process to terminate """
    print("TestEcho: %s - stopping processes :%s" % (msg, proc.pid))
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
        print("TestEcho: Again stopping processes :%s" % proc.pid)
        procrtn = -1
        try:
            proc.terminate()
            proc.wait(2)
        except ProcessLookupError:
            pass
        except Exception:
            print("TestEcho: killing processes :%s" % proc.pid)
            proc.kill()

    print("TestEcho: %s - return code: %d\n" % (msg, procrtn))
    return procrtn

def logdir_name(fullname):
    """create the log directory name"""
    names = fullname.split('.')
    items = names[-1].split('_', maxsplit=2)
    return "/" + items[2]

def add_prefix_logdir(testcase_id):
    """add the log directory to the prefix"""
    global NPROC
    prefix = ""
    ompi_bin = os.getenv('CRT_OMPI_BIN', "")
    log_path = os.getenv("CRT_TESTLOG", "echo_test") + logdir_name(testcase_id)
    os.makedirs(log_path, exist_ok=True)
    use_valgrind = os.getenv('TR_USE_VALGRIND', default="")
    if use_valgrind == 'memcheck':
        NPROC = "2"
        suppressfile = os.path.join(os.getenv('CRT_PREFIX', ".."), "etc", \
                       "memcheck-mcl.supp")
        prefix = "valgrind --xml=yes" + \
            " --xml-file=" + log_path + "/valgrind.%q{PMIX_ID}.xml" + \
            " --leak-check=yes --gen-suppressions=all" + \
            " --suppressions=" + suppressfile + " --show-reachable=yes"
    elif use_valgrind == "callgrind":
        NPROC = "2"
        prefix = "valgrind --tool=callgrind --callgrind-out-file=" + \
                 log_path + "/callgrind.%q{PMIX_ID}.out"
    else:
        # needed to clear any preceding value
        NPROC = "1"

    if os.path.exists("./orted-uri"):
        dvmfile = " --hnp file:orted-uri "
    else:
        dvmfile = " "
    if getpass.getuser() == "root":
        allow_root = " --allow-run-as-root"
    else:
        allow_root = ""
    cmdstr = "%sorterun%s--output-filename %s%s" % \
             (ompi_bin, dvmfile, log_path, allow_root)

    return (cmdstr, prefix)

def add_server_client():
    """create the server and client prefix"""
    server = os.getenv('CRT_TEST_SERVER')
    if server:
        local_server = " -H %s " % server
    else:
        local_server = " "
    client = os.getenv('CRT_TEST_CLIENT')
    if client:
        local_client = " -H %s " % client
    else:
        local_client = " "

    return (local_server, local_client)


class TestEcho(unittest.TestCase):
    """ Execute process set tests """
    pass_env = " -x PATH -x LD_LIBRARY_PATH -x CCI_CONFIG "

    def one_node_echo_test(self):
        """Simple process set test 1"""
        testmsg = self.shortDescription()
        (cmd, prefix) = add_prefix_logdir(self.id())
        (server, client) = add_server_client()
        cmdstr = cmd + \
          "%s-np %s %s%s tests/crt_echo_srv :" % \
          (server, NPROC, self.pass_env, prefix) + \
          "%s-np %s %s%s tests/crt_echo_cli" % \
          (client, NPROC, self.pass_env, prefix)
        procrtn = launch_test(testmsg, cmdstr)
        return procrtn

    def two_node_echo_test(self):
        """Simple process set test 1"""
        testmsg = self.shortDescription()
        print("test name: %s" % self.id())
        (cmd, prefix) = add_prefix_logdir(self.id() + "server_node")
        (server, client) = add_server_client()
        cmdstr = cmd + \
          "%s-np %s %s%s tests/crt_echo_srv :" % \
          (server, NPROC, self.pass_env, prefix)
        proc_srv = launch_process(testmsg, cmdstr)
        (cmd, prefix) = add_prefix_logdir(self.id() + "client_node")
        cmdstr = cmd + \
          "%s-np %s %s%s tests/crt_echo_cli" % \
          (client, NPROC, self.pass_env, prefix)
        procrtn = launch_test(testmsg, cmdstr)
        procrtn |= stop_process(testmsg, proc_srv)
        return procrtn

    def test_echo_test(self):
        """Simple process set test 1"""
        if os.path.exists("./orted-uri"):
            self.assertFalse(self.two_node_echo_test())
        else:
            self.assertFalse(self.one_node_echo_test())

    def setUp(self):
        """teardown module for test"""
        print("******************************************************")
        print("TestEcho: begin %s " % self.shortDescription())

    def tearDown(self):
        """teardown module for test"""
        print("TestEcho: tearDown begin")
        testmsg = "terminate any crt_echo processes"
        cmdstr = "pkill crt_echo"
        launch_test(testmsg, cmdstr)
        print("TestEcho: end  %s"  % self.shortDescription())
        print("******************************************************")
