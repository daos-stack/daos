#!/usr/bin/env python3
# Copyright (c) 2016-2017 Intel Corporation
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
    report = ""
    logfileout = ""
    logfilerr = ""

    def __init__(self, info=None):
        self.info = info

    def launch_process(self):
        """Launch otred processes """
        print("TestRunner: start orte-dvm process\n")
        hosts = ","
        log_path = self.info.get_config('log_base_path')
        if not os.path.exists(log_path):
            os.makedirs(log_path)
        os.environ['OMPI_MCA_rmaps_base_oversubscribe'] = "1"
        self.report = os.path.join(log_path, "orted-uri")
        self.info.set_config('setKeyFromConfig', 'TR_USE_URI', self.report)
        self.logfileout = os.path.join(log_path, "orte-dvm.out")
        self.logfilerr = os.path.join(log_path, "orte-dvm.err")
        ompi_path = self.info.get_info('OMPI_PREFIX')
        dvm = os.path.join(ompi_path, "bin", "orte-dvm")
        hostlist = hosts.join(self.info.get_config('host_list'))
        cmdstr = "%s --prefix %s --report-uri %s --host %s" % \
                 (dvm, ompi_path, self.report, hostlist)
        cmdarg = shlex.split(cmdstr)
        with open(self.logfileout, mode='w') as outfile, \
            open(self.logfilerr, mode='w') as errfile:
            outfile.write("=======================================\n " + \
                          " Command: " + str(cmdstr) + \
                          "\n======================================\n")
            outfile.flush()
            self.ortedvm = subprocess.Popen(cmdarg,
                                            stdin=subprocess.DEVNULL,
                                            stdout=outfile,
                                            stderr=errfile)
        # wait for DVM to start
        print("TestRunner: orte-dvm process wait")
        time.sleep(10)
        print("TestRunner: orte-dvm process started pid: %d \n" %
              self.ortedvm.pid)
        if self.ortedvm.poll() is None:
            return 0
        else:
            print("TestRunner: orte-dvm failed to start")
            print("TestRunner: orte-dvm rc: %d\n" % self.ortedvm.returncode)
            with open(self.logfilerr, mode='r') as fd:
                print("TestRunner: orte-dvm STDERR:\n %s" % fd.read())
            os.remove(self.report)
            return 1

    def stop_process(self):
        """stop orted processes """
        print("TestRunner: stopping orte-dvm process\n")
        if self.ortedvm.poll() is None:
            hosts = ","
            ompi_path = self.info.get_info('OMPI_PREFIX')
            orterun = os.path.join(ompi_path, "bin", "orterun")
            hostlist = hosts.join(self.info.get_config('host_list'))
            cmdstr = "%s --terminate --prefix %s --hnp file:%s --host %s" % \
                     (orterun, ompi_path, self.report, hostlist)
            cmdarg = shlex.split(cmdstr)
            try:
                subprocess.call(cmdarg, timeout=10)
            except subprocess.TimeoutExpired:
                self.ortedvm.terminate()
                try:
                    self.ortedvm.wait(timeout=1)
                except subprocess.TimeoutExpired:
                    print("TestRunner: orte-dvm termination may have failed")
        if self.ortedvm.returncode:
            print("TestRunner: orte-dvm rc: %d\n" % self.ortedvm.returncode)
        with open(self.logfileout, mode='r') as fd:
            print("TestRunner: orte-dvm STDOUT:\n %s" % fd.read())
        with open(self.logfilerr, mode='r') as fd:
            print("TestRunner: orte-dvm STDERR:\n %s" % fd.read())
        os.remove(self.report)
