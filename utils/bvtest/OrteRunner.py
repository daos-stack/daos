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
# -*- coding: utf-8 -*-
"""
class to execute a command using orte
"""

import os
import subprocess
import shlex
import time
import getpass
import logging
#pylint: disable=import-error
from PostRunner import check_log_mode

#pylint: disable=broad-except

class OrteRunner():
    """setup for using ompi from test runner"""
    proc = None
    log_dir_orte = ""
    testsuite = ""

    def __init__(self, test_info, log_path, testsuite, prefix):
        """add the log directory to the prefix
           Note: entries, after the first, start with a space"""
        self.test_info = test_info
        self.testsuite = testsuite
        self.log_dir_orte = log_path
        self.logger = logging.getLogger("TestRunnerLogger")
        self.cmd_list = []
        self.cmd_list.append("{!s}orterun".format(prefix))
        if self.test_info.get_defaultENV('TR_USE_URI', ""):
            self.cmd_list.append(" --hnp file:{!s}".format(
                self.test_info.get_defaultENV('TR_USE_URI')))
        self.cmd_list.append(" --output-filename {!s}".
                             format(self.log_dir_orte))
        if getpass.getuser() == "root":
            self.cmd_list.append(" --allow-run-as-root")

    def dump_cmd(self, msg):
        """log the cmd_list"""
        self.logger.info("%s: %s", msg, str(self.cmd_list))

    def next_cmd(self):
        """add the : to start a new command string
           Note: added entries start with a space"""
        self.cmd_list.append(" :")

    def add_cmd(self, cmd, parameters=""):
        """add the ommand and parameters to the list
           Note: added entries start with a space"""
        self.cmd_list.append(" {!s}".format(cmd))
        if parameters:
            self.cmd_list.append(" {!s}".format(parameters))

    def add_env_vars(self, env_vars):
        """add the environment variables to the command list
           Note: entries start with a space"""
        for (key, value) in env_vars.items():
            if value:
                self.cmd_list.append(" -x {!s}={!s}".format(key, value))
            else:
                self.cmd_list.append(" -x {!s}".format(key))

    def add_nodes(self, nodes, procs=1):
        """add the node prefix to the command list
           Note: entries start with a space"""
        if nodes[0].isupper():
            node_list = self.test_info.get_defaultENV(nodes)
        else:
            node_list = nodes

        self.cmd_list.append(" -H {!s} -N {!s}".format(node_list, procs))

    def add_param(self, param_str):
        """add orte options to command string
           Note: added entries start with a space
           step 1a optional next step 2
           this step can be called multiple time before step 2 """
        self.cmd_list.append(" {!s}".format(param_str))

    def start_process(self):
        """Launch process set """
        cmdstr = ''.join(self.cmd_list)
        self.logger.info("OrteRunner: start: %s", cmdstr)
        cmdarg = shlex.split(cmdstr)
        fileout = os.path.join(self.log_dir_orte,
                               "{!s}.out".format(self.testsuite))
        fileerr = os.path.join(self.log_dir_orte,
                               "{!s}.err".format(self.testsuite))
        with open(fileout, mode='a') as outfile, \
            open(fileerr, mode='a') as errfile:
            outfile.write("{!s}\n  Command: {!s} \n{!s}\n".format(
                ("=" * 40), cmdstr, ("=" * 40)))
            outfile.flush()
            errfile.write("{!s}\n  Command: {!s} \n{!s}\n".format(
                ("=" * 40), cmdstr, ("=" * 40)))
            self.proc = subprocess.Popen(cmdarg, stdout=outfile, stderr=errfile)

    def check_process(self):
        """Check if a process is still running"""
        self.proc.poll()
        procrtn = self.proc.returncode
        if procrtn is None:
            return True
        self.logger.info("Process has exited")
        return False

    def wait_process(self, waittime=180):
        """wait for processes to terminate
        Wait for the process to exit, and return the return code.
        """
        self.logger.info("Test: waiting for process :%s", self.proc.pid)

        try:
            procrtn = self.proc.wait(waittime)
            check_log_mode(self.log_dir_orte)
        except subprocess.TimeoutExpired as e:
            self.logger.info("Test: process timeout: %s\n", e)
            procrtn = self.stop_process("process timeout")

        self.logger.info("Test: return code: %s\n", procrtn)
        return procrtn


    def stop_process(self, msg):
        """ wait for process to terminate """
        self.logger.info("%s: %s - stopping processes :%s", \
          self.testsuite, msg, self.proc.pid)
        i = 60
        procrtn = None
        while i:
            self.proc.poll()
            procrtn = self.proc.returncode
            if procrtn is not None:
                break
            else:
                time.sleep(1)
                i = i - 1

        if procrtn is None:
            self.logger.info("%s: Again stopping processes :%s", \
              self.testsuite, self.proc.pid)
            procrtn = -1
            try:
                self.proc.terminate()
                self.proc.wait(2)
            except ProcessLookupError:
                pass
            except Exception:
                self.logger.info("%s: killing processes :%s", \
                  self.testsuite, self.proc.pid)
                self.proc.kill()

        self.logger.info("%s: %s - return code: %d\n", \
          self.testsuite, msg, procrtn)
        check_log_mode(self.log_dir_orte)
        return procrtn
