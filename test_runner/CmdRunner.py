#!/usr/bin/env python
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

""" execute a command on local node """

import os
import shlex
import subprocess
import logging


class CmdRunner:
    """Simple command controller """
    outfile = ""

    def __init__(self, log_path, log_name):
        self.log_path = log_path
        self.logger = logging.getLogger("TestRunnerLogger")
        self.state = "init"
        self.proc = None
        self.procrtn = 0
        self.outfile = os.path.join(log_path, ("{!s}.out".format(log_name)))

    def execute_cmd(self, cmd, args, cwd=None, environ=None):
        """
        Run specified cmd+args on the node
        """
        self.logger.info("TestNodeRunner: start command %s", cmd)
        cmdstr = "{!s} {!s}".format(cmd, args)
        cmdarg = shlex.split(cmdstr)
        with open(self.outfile, mode='a') as outfile:
            outfile.write("{!s}\n  Command: {!s} \n{!s}\n".format(
                ("=" * 40), cmdstr, ("=" * 40)))
            rtn = subprocess.Popen(cmdarg,
                                   stdout=outfile,
                                   stderr=subprocess.STDOUT,
                                   cwd=cwd,
                                   env=environ)

        self.proc = rtn
        self.state = "running"
        self.procrtn = None

    def process_state(self):
        """ poll remote processes """
        if self.state == "running":
            if self.proc.poll() is not None:
                self.state = "done"
                self.procrtn = self.proc.returncode
        return self.state

    def process_rtn(self):
        """ poll remote processes """
        return self.procrtn

    def process_terminate(self):
        """ poll remote processes """
        if self.proc.poll() is None:
            self.proc.terminate()
            self.state = "terminate"
