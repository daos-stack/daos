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
orte-dvm runner class

"""

import os
import subprocess
import shlex
import time


class DvmRunner():
    """Simple test runner"""
    info = None
    ortedvm = None
    report = "orted-uri"

    def __init__(self, info=None):
        self.info = info

    def launch_process(self):
        """Launch otred processes """
        print("TestRunner: start orte-dvm process\n")
        hosts = ","
        ompi_path = self.info.get_info('OMPI_PREFIX')
        hostlist = hosts.join(self.info.get_config('host_list'))
        cmdstr = "orte-dvm --prefix %s --report-uri %s --host %s" % \
                 (ompi_path, self.report, hostlist)
        cmdarg = shlex.split(cmdstr)
        with open("orte-dvm.out", mode='w+b', buffering=0) as outfile:
            self.ortedvm = subprocess.Popen(cmdarg,
                                            stdin=subprocess.DEVNULL,
                                            stdout=outfile,
                                            stderr=subprocess.STDOUT)

        # wait for DVM to start
        time.sleep(5)
        return not self.ortedvm

    def stop_process(self):
        """stop orted processes """
        print("TestRunner: stopping orte-dvm process\n")
        self.ortedvm.terminate()
        self.ortedvm.wait(timeout=1)
        print("TestRunner: orte-dvm rc: %d\n" % self.ortedvm.returncode)
        with open("orte-dvm.out", mode='r') as fd:
            print("anything %s" % fd.read())
        os.remove(self.report)
