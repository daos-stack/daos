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
"""
iof common test suite

Usage:

The common module is imported and executed by the IOF test suites.
The module contains the common/supporting functions for the tests:
setUp, tearDown, create the input/command string and set the log dir.

The results are placed in the testLogs/nss directory.
Any test_ps output is under
<file yaml>_loop#/<module.name.execStrategy.id>/1(process set)/rank<number>.
There you will find anything written to stdout and stderr. The output from
memcheck and callgrind are in the nss directory. At the end of a test run,
the last nss directory is renamed to nss<date stamp>

To use valgrind memory checking
set TR_USE_VALGRIND in iof_test_ionss.yml to memcheck

To use valgrind call (callgrind) profiling
set TR_USE_VALGRIND in iof_test_ionss.yml to callgrind

To redirect output to the log file,
set TR_REDIRECT_OUTPUT in iof_test_ionss.yml
The test_runner wil set it to redirect the output to the log file.
By default the output is displayed on the screen.
"""
#pylint: disable=too-many-locals
#pylint: disable=broad-except
import os
import json
import subprocess
import time
import logging

CART_PREFIX = None
SELF_PREFIX = None

def load_config():
    """Load the build data from the json file"""
    global CART_PREFIX
    global SELF_PREFIX

    fd = open('.build_vars-Linux.json')
    data = json.load(fd)
    fd.close()
    CART_PREFIX = data['CART_PREFIX']
    SELF_PREFIX = data['PREFIX']
    return data

def valgrind_iof_supp_file():
    """Return the path of the IOF memcheck suppression file"""

    iof_test_bin = os.getenv('IOF_TEST_BIN')
    if iof_test_bin:
        prefix_dir = os.path.join(iof_test_bin, '..', 'etc')
    elif SELF_PREFIX:
        prefix_dir = os.path.join(SELF_PREFIX, 'etc')
    else:
        prefix_dir = os.path.join(os.path.dirname(os.path.realpath(__file__)),
                                  '..', 'utils')

    iof_suppressfile = os.path.join(prefix_dir, 'memcheck-iof.supp')
    return os.path.realpath(iof_suppressfile)

def valgrind_cart_supp_file():
    """Return the path of the CaRT memcheck suppression file"""

    cart_prefix = os.getenv('IOF_CART_PREFIX', CART_PREFIX)
    if not cart_prefix:
        return None

    crt_suppressfile = os.path.join(cart_prefix,
                                    'etc',
                                    'memcheck-cart.supp')
    if not os.path.exists(crt_suppressfile):
        return None
    return crt_suppressfile

def valgrind_suffix(log_path, pmix=True):
    """Return the commands required to launch valgrind"""
    use_valgrind = os.getenv('TR_USE_VALGRIND', default=None)
    crt_suppressfile = valgrind_cart_supp_file()
    iof_suppressfile = valgrind_iof_supp_file()
    pid = '%p'
    if pmix:
        pid = '%q{PMIX_RANK}'

    if use_valgrind == 'memcheck':
        cmd = ['valgrind',
               '--fair-sched=try',
               '--free-fill=0x87',
               '--xml=yes',
               '--xml-file=%s' %
               os.path.join(log_path,
                            "valgrind-%s.memcheck" % pid),
               '--sim-hints=fuse-compatible',
               '--leak-check=full', '--gen-suppressions=all',
               '--fullpath-after=',
               '--partial-loads-ok=yes',
               '--suppressions=%s' % iof_suppressfile,
               '--show-reachable=yes']
        if crt_suppressfile:
            cmd.append('--suppressions=%s' % crt_suppressfile)
        return cmd
    if use_valgrind == "callgrind":
        return ['valgrind',
                '--fair-sched=try',
                '--tool=callgrind',
                '--callgrind-out-file=%s' %
                os.path.join(log_path,
                             "callgrind-%s.in" % pid)]
    if use_valgrind == "memcheck-native":
        cmd = ['valgrind',
               '--fair-sched=try',
               '--sim-hints=fuse-compatible',
               '--error-exitcode=42',
               '--log-file=%s' %
               os.path.join(log_path,
                            "valgrind-%s.txt" % pid),
               '--leak-check=full', '--gen-suppressions=all',
               '--fullpath-after=',
               '--partial-loads-ok=yes',
               '--suppressions=%s' % iof_suppressfile,
               '--show-reachable=yes']
        if crt_suppressfile:
            cmd.append('--suppressions=%s' % crt_suppressfile)
        return cmd
    return []

class CommonTestSuite():
    """Attributes common to the IOF tests"""
    logger = logging.getLogger("TestRunnerLogger")

    def logdir_name(self):
        """create the log directory name"""
        pass

    def common_launch_cmd(self, cmd, timeout=180):
        """Launch a test and wait for it to complete"""

        cmdarg = cmd
        msg = cmd[0].split('/')[-1]
        self.logger.info("Testnss: start %s - input string:\n %s\n", \
                         msg,
                         ' '.join(cmd))

        to_redirect = os.getenv('TR_REDIRECT_OUTPUT', "no").lower()

        # If TR_REDIRECT_OUTPUT = "no"; the output is redirected to the screen
        # else if TR_REDIRECT_OUTPUT = "null"; the ouput goes to devnull
        # else if TR_REDIRECT_OUTPUT = "yes"; the output goes to a file.
        if to_redirect == "no":
            procrtn = subprocess.call(cmdarg, timeout=timeout)
        elif to_redirect == "null":
            procrtn = subprocess.call(cmdarg, timeout=timeout,
                                      stdout=subprocess.DEVNULL,
                                      stderr=subprocess.DEVNULL)
        else:
            log_path = os.path.join(os.getenv("IOF_TESTLOG",
                                              os.path.join(os.path.dirname(
                                                  os.path.realpath(__file__)),
                                                           'output')),
                                    self.logdir_name())
            if not os.path.exists(log_path):
                os.makedirs(log_path)
            cmdfileout = os.path.join(log_path, "common_launch_test.out")
            cmdfileerr = os.path.join(log_path, "common_launch_test.err")
            procrtn = -1
            try:
                with open(cmdfileout, mode='a') as outfile, \
                    open(cmdfileerr, mode='a') as errfile:
                    procrtn = subprocess.call(cmdarg, timeout=timeout,
                                              stdout=outfile,
                                              stderr=errfile)
            except (FileNotFoundError) as e:
                self.logger.info("Testnss: %s", \
                                 e.strerror)
            except (IOError) as e:
                self.logger.info("Testnss: Error opening the log files: %s", \
                                 e.errno)

        return procrtn

    def common_launch_process(self, cmd):
        """Launch a process"""

        cmdarg = cmd
        msg = cmd[0].split('/')[-1]
        self.logger.info("Testnss: start %s - input string:\n %s\n", \
                        msg,
                         ' '.join(cmd))

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
            log_path = os.path.join(os.getenv("IOF_TESTLOG",
                                              os.path.join(os.path.dirname(
                                                  os.path.realpath(__file__)),
                                                           'output')),
                                    self.logdir_name())
            if not os.path.exists(log_path):
                os.makedirs(log_path)
            cmdfileout = os.path.join(log_path, "common_launch_process.out")
            cmdfileerr = os.path.join(log_path, "common_launch_process.err")
            with open(cmdfileout, mode='a') as outfile, \
                open(cmdfileerr, mode='a') as errfile:
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

    def common_stop_process(self, proc):
        """wait for processes to terminate

        Wait for up to 60 seconds for the process to die on it's own, then if
        still running attept to kill it.

        Return the error code of the process, or -1 if the process was killed.
        """
        self.logger.info("Test: stopping processes :%s", proc.pid)
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
            procrtn = -1
            try:
                self.logger.info("Test: terminating processes :%s", proc.pid)
                proc.terminate()
                proc.wait(2)
            except ProcessLookupError:
                pass
            except Exception:
                self.logger.info("Killing processes: %s", proc.pid)
                proc.kill()

        self.logger.info("Test: return code: %s\n", procrtn)
        return procrtn

    def wait_process(self, proc):
        """wait for processes to terminate

        Wait forever for the process to exit, and return the return code.
        """
        self.logger.info("Test: waiting for process :%s", proc.pid)

        procrtn = proc.wait()

        self.logger.info("Test: return code: %s\n", procrtn)
        return procrtn
