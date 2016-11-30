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


class NodeRunner():
    """Simple node controller """
    node = ""

    def __init__(self, info, node, dir_path, scripts_dir, directives):
        self.info = info
        self.node = node
        self.dir_path = dir_path
        self.scripts_dir = scripts_dir
        self.test_directives = directives
        self.logger = logging.getLogger("TestRunnerLogger")
        self.logger.setLevel(logging.DEBUG)
        self.test_config = {}
        self.test_name = ""
        self.state = "init"
        self.proc = None
        self.logfileout = ""
        self.logfileerr = ""

    def launch_test(self):
        """ Launch remote processes """
        self.logger.info("TestRunner: start remote process %s", self.node)
        self.logger.debug("conf: " + str(self.test_config))
        test_name = self.test_name
        configfile = test_name + "_config"
        log_path = self.test_config['log_base_path']
        self.logger.info("config log path: %s", log_path)
        test_config = os.path.abspath(os.path.join(log_path, configfile))
        self.logger.debug("writing config: %s", test_config)
        with open(test_config, "w") as config_info:
            json.dump(self.test_config, config_info, skipkeys=True, indent=4)

        test_yml = os.path.join(self.scripts_dir, "%s.yml" % test_name)
        node = self.node
        self.logfileout = os.path.join(log_path, ("%s.out" % self.test_name))
        self.logfileerr = os.path.join(log_path, ("%s.err" % self.test_name))
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
        #                           stdin=subprocess.DEVNULL,

        self.proc = rtn
        self.state = "running"

    def process_state(self):
        """ poll remote processes """
        if self.state is "running":
            if self.proc.poll() is not None:
                self.state = "done"
        return self.state

    def process_rtn(self):
        """ poll remote processes """
        return self.proc.returncode

    def process_terminate(self):
        """ poll remote processes """
        if self.proc.poll() is None:
            self.proc.terminate()
            #try:
            #    self.proc.wait(timeout=1)
            #except subprocess.TimeoutExpired:
            #    print("NodeRunner: termination may have failed")

        return self.proc.returncode

    def dump_files(self):
        """ Launch reomte processes """
        with open(self.logfileout, mode='r') as fd:
            print("STDOUT: %s" % fd.rad())
        with open(self.logfileerr, mode='r') as fd:
            print("STDERR:\n %s" % fd.read())

    def setup_config(self, name, logdir, setFromConfig=None, directives=None):
        """ setup base config """
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
        if setFromConfig:
            self.test_config['setKeyFromConfig'] = {}
            self.test_config['setKeyFromConfig'].update(setFromConfig)

        # add testing directives
        self.test_config['setDirectiveFromConfig'] = {}
        if directives:
            self.test_config['setDirectiveFromConfig'].update(directives)
        self.test_config['setDirectiveFromConfig']['renameTestRun'] = "no"
        self.logger.debug("conf: " + str(self.test_config))
