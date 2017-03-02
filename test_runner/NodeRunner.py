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

""" execute test runner on a node """

#pylint: disable=too-many-instance-attributes
#pylint: disable=too-many-arguments

import os
import shlex
import subprocess
import logging
import json
import shutil
from yaml import load
try:
    from yaml import CLoader as Loader
except ImportError:
    from yaml import Loader


class NodeRunner():
    """Simple node controller """
    node = ""

    def __init__(self, info, node, dir_path, scripts_dir, directives,
                 node_type='all'):
        self.info = info
        self.node = node
        self.node_type = node_type
        self.dir_path = dir_path
        self.scripts_dir = scripts_dir
        self.test_directives = directives
        self.logger = logging.getLogger("TestRunnerLogger")
        self.test_config = {}
        self.test_name = ""
        self.state = "init"
        self.proc = None
        self.procrtn = 0
        self.logfileout = ""
        self.logfileerr = ""
        self.cmdfileout = ""
        self.cmdfileerr = ""

    def run_cmd(self, cmd, log_path, node_type='all'):
        """ Launch remote command """
        if node_type != self.node_type and node_type != 'all' and \
           self.node_type != 'all':
            self.procrtn = 0
            return
        node = self.node
        self.logger.info("TestRunner: start command %s on %s", cmd, node)
        self.cmdfileout = os.path.join(log_path, ("cmd_%s.out" % node))
        self.cmdfileerr = os.path.join(log_path, ("cmd_%s.err" % node))
        cmdstr = "ssh %s \'%s \'" % (node, cmd)
        cmdarg = shlex.split(cmdstr)
        with open(self.cmdfileout, mode='a') as outfile, \
            open(self.cmdfileerr, mode='a') as errfile:
            outfile.write("============================\n " + str(cmd) + " \n")
            errfile.write("============================\n " + str(cmd) + " \n")
            rtn = subprocess.Popen(cmdarg,
                                   stdout=outfile,
                                   stderr=errfile)

        self.proc = rtn
        self.state = "running"
        self.procrtn = None

    def launch_test(self, node_type='all'):
        """ Launch remote test runner """
        if node_type != self.node_type and node_type != 'all' and \
           self.node_type != 'all':
            self.procrtn = 0
            return
        test_name = self.test_name
        self.logger.info("TestRunner: start %s on %s", test_name, self.node)
        self.logger.debug("conf: " + str(self.test_config))
        configfile = test_name + "_config"
        log_path = self.test_config['log_base_path']
        self.logger.info("log path: %s", log_path)
        test_config = os.path.abspath(os.path.join(log_path, configfile))
        self.logger.debug("writing config: %s", test_config)
        with open(test_config, "w") as config_info:
            json.dump(self.test_config, config_info, skipkeys=True, indent=4)

        test_yml = os.path.join(self.scripts_dir, "%s.yml" % test_name)
        node = self.node
        self.logfileout = os.path.join(log_path, ("%s.out" % test_name))
        self.logfileerr = os.path.join(log_path, ("%s.err" % test_name))
        python_vers = self.test_directives.get('usePython', "python3.4")
        cmdstr = "ssh %s \'%s %s/test_runner config=%s %s\'" % \
            (node, python_vers, self.dir_path, test_config, test_yml)
        self.logger.debug("cmd: %s", cmdstr)
        cmdarg = shlex.split(cmdstr)
        with open(self.logfileout, mode='w+b', buffering=0) as outfile, \
            open(self.logfileerr, mode='w+b', buffering=0) as errfile:
            rtn = subprocess.Popen(cmdarg,
                                   stdout=outfile,
                                   stderr=errfile)

        self.proc = rtn
        self.state = "running"
        self.procrtn = None

    def process_state(self):
        """ poll remote processes """
        if self.state is "running":
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
            #try:
            #    self.proc.wait(timeout=1)
            #except subprocess.TimeoutExpired:
            #    print("NodeRunner: termination may have failed")

        return self.proc.returncode

    def match_testName(self):
        """ match the name of the log log to the testcase """
        subtest_results_file = os.path.join(self.test_config['log_base_path'],
                                            "subtest_results.yml")
        if not os.path.exists(subtest_results_file):
            return
        with open(subtest_results_file, 'r') as fd:
            subtest_results_data = load(fd, Loader=Loader)
        test = str(subtest_results_data[0]['name'])

        for logfile in [self.logfileout, self.logfileerr]:
            (path, name) = os.path.split(logfile)
            namePlus = name.split('.')
            if test == namePlus[0]:
                break
            shutil.move(logfile,
                        os.path.join(path,
                                     "{!s}.{!s}".format(test, namePlus[1])))

    def dump_files(self):
        """ dump the log files """
        with open(self.logfileout, mode='r') as fd:
            print("STDOUT: %s" % fd.rad())
        with open(self.logfileerr, mode='r') as fd:
            print("STDERR:\n %s" % fd.read())

    def dump_info(self):
        """ print info about node """
        self.logger.info("node: %s  type: %s", self.node, self.node_type)

    def setup_config(self, name, logdir, node_type='all',
                     setFromConfig=None, directives=None):
        """ setup base config """
        if node_type != self.node_type and node_type != 'all' and \
           self.node_type != 'all':
            self.test_name = ""
            return
        self.test_name = name
        self.test_config.clear()

        copyList = self.test_directives.get('copyHostList', "yes")
        if copyList is "yes":
            self.test_config['host_list'] = self.info.get_config('host_list')
        build_path = self.info.get_config('build_path')
        if not build_path:
            build_path = os.path.dirname(os.getcwd())
        self.test_config['build_path'] = build_path
        self.test_config['log_base_path'] = logdir
        self.test_config['node'] = self.node
        self.test_config['node_type'] = self.node_type
        if setFromConfig:
            self.test_config['setKeyFromConfig'] = {}
            self.test_config['setKeyFromConfig'].update(setFromConfig)

        # add testing directives
        self.test_config['setDirectiveFromConfig'] = {}
        if directives:
            self.test_config['setDirectiveFromConfig'].update(directives)
        self.test_config['setDirectiveFromConfig']['renameTestRun'] = "no"
        self.logger.debug("conf: " + str(self.test_config))
