#!/usr/bin/env python3
# Copyright (c) 2016-2018 Intel Corporation
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

class DvmRunner():
    """Simple test runner"""
    info = None
    ortedvm = None
    report = ""
    logfileout = ""

    def __init__(self, info=None):
        self.info = info
        self.hostlist = "localhost"

    def launch_process(self):
        """Launch otred processes """
        print("TestRunner: start orte-dvm process\n")
        log_path = self.info.get_config('log_base_path')
        if not os.path.exists(log_path):
            os.makedirs(log_path)
        os.environ['OMPI_MCA_rmaps_base_oversubscribe'] = "1"
        self.report = os.path.join(log_path, "orted-uri")
        self.info.set_config('setKeyFromConfig', 'TR_USE_URI', self.report)
        self.logfileout = os.path.join(log_path, "orte-dvm.out")
        self.hostlist = ",".join(self.info.get_config('host_list'))

        return 0

    def stop_process(self):
        """stop orted processes """
        print("DvmRunner: stopping, report uri was {}".format(self.report))
        return 0
