#!/usr/bin/env python3
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

    def launch_dvm_process(self):
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

    def stop_dvm_process(self):
        """stop orted processes """
        print("TestRunner: stopping orte-dvm process\n")
        self.ortedvm.terminate()
        self.ortedvm.wait(timeout=1)
        print("TestRunner: orte-dvm rc: %d\n" % self.ortedvm.returncode)
        with open("orte-dvm.out", mode='r') as fd:
            print("anything %s" % fd.read())
        os.remove(self.report)
