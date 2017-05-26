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

""" execute a command on a node """

#pylint: disable=too-many-instance-attributes
#pylint: disable=too-many-arguments

import os
import shlex
import subprocess
import logging


class NodeRunner():
    """Simple node controller """

    def __init__(self, info, node, node_type='all'):
        self.test_info = info
        self.node = node
        self.node_type = node_type
        self.logger = logging.getLogger("TestRunnerLogger")
        self.state = "init"
        self.proc = None
        self.procrtn = 0
        self.cmdfileout = ""
        self.cmdfileerr = ""

    def run_cmd(self, cmd, log_path):
        """ Launch remote command """
        node = self.node
        self.logger.info("TestNodeRunner: start command %s on %s", cmd, node)
        self.cmdfileout = os.path.join(log_path, ("cmd_%s.out" % node))
        self.cmdfileerr = os.path.join(log_path, ("cmd_%s.err" % node))
        cmdstr = "ssh %s \'%s \'" % (node, cmd)
        cmdarg = shlex.split(cmdstr)
        with open(self.cmdfileout, mode='a') as outfile, \
            open(self.cmdfileerr, mode='a') as errfile:
            outfile.write("{!s}\n  Command: {!s} \n{!s}\n".format(
                ("=" * 40), cmdstr, ("=" * 40)))
            errfile.write("{!s}\n  Command: {!s} \n{!s}\n".format(
                ("=" * 40), cmdstr, ("=" * 40)))
            rtn = subprocess.Popen(cmdarg,
                                   stdout=outfile,
                                   stderr=errfile)

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
