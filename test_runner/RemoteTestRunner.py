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

"""
execute test runner on a node

This class is used by Multi Runner to execute and control a copy of Test Runner
on a remote node. The class uses its assigned node type classification to
determine if it should to execute a request. The information about a request is
contained test_config map. The information with the test_config will be
writtern to the log directory for request and becomes the config file supplied
to Test Runner. The class has methods for controlling the new Test Runner
process.

"""

#pylint: disable=too-many-instance-attributes
#pylint: disable=too-many-arguments

import os
import logging
import paramiko
import json
from yaml import load
try:
    from yaml import CLoader as Loader
except ImportError:
    from yaml import Loader


class RemoteTestRunner():
    """Simple node controller """

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
        self.stdout = None
        self.stderr = None
        self.procrtn = 0
        self.logfileout = ""
        self.logfileerr = ""
        self.username = os.getlogin()
        self.client = paramiko.SSHClient()
        self.client.set_missing_host_key_policy(paramiko.AutoAddPolicy())

    def match_type(self, node_type="all"):
        """ match the node requested node type to this nodes type """
        self.procrtn = 0
        self.state = "done"
        if node_type != self.node_type and node_type != 'all' and \
           self.node_type != 'all':
            return False
        return True

    def connect(self):
        """ connect to remote system """
        connect_args = {}
        connect_args['hostname'] = self.node
        connect_args['username'] = self.username
        if self.test_directives.get('useKeyFile'):
            connect_args['key_filename'] = \
                self.test_directives.get('useKeyFile')
        connect_args['allow_agent'] = False
        connect_args['look_for_keys'] = True
        connect_args['timeout'] = 15
        self.client.connect(**connect_args)

    def launch_test(self, timeout=180):
        """ Launch remote test runner """
        test_name = self.test_name
        self.logger.info("TestRunner: start %s on %s", test_name, self.node)
        self.connect()
        self.logger.debug("conf: " + str(self.test_config))
        configfile = test_name + "_config"
        log_path = self.test_config['log_base_path']
        self.logger.info("log path: %s", log_path)
        test_config = os.path.abspath(os.path.join(log_path, configfile))
        self.logger.debug("writing config: %s", test_config)
        with open(test_config, "w") as config_info:
            json.dump(self.test_config, config_info, skipkeys=True, indent=4)

        test_yml = os.path.join(self.scripts_dir, "{!s}.yml".format(test_name))
        self.logfileout = os.path.join(log_path,
                                       ("{!s}.runout".format(test_name)))
        self.logfileerr = os.path.join(log_path,
                                       ("{!s}.runerr".format(test_name)))
        python_vers = self.test_directives.get('usePython', "python3.4")
        cmdstr = "{!s} {!s}/test_runner config={!s} {!s}".format(
            python_vers, self.dir_path, test_config, test_yml)
        self.logger.debug("cmd: %s", cmdstr)
        with open(self.logfileout, mode='w') as outfile:
            outfile.write("{!s}\n  Command: {!s} \n{!s}\n".format(
                ("=" * 40), cmdstr, ("=" * 40)))
            outfile.flush()
        dummy_stdin, self.stdout, self.stderr = \
            self.client.exec_command(cmdstr, timeout=timeout)
        dummy_stdin.close()
        self.state = "running"
        self.procrtn = None

    def dump_data(self):
        """ dump the output to a file """
        with open(self.logfileout, mode='a') as outfile:
            for line in self.stdout.readlines():
                outfile.write(line)
        with open(self.logfileerr, mode='a') as errfile:
            errfile.write("Command complete within timeout.")
            for line in self.stderr.readlines():
                errfile.write(line)
        self.client.close()

    def process_state(self):
        """ poll remote processes for state """
        if self.state == "running":
            if self.stdout.channel.exit_status_ready():
                self.state = "done"
                self.procrtn = self.stdout.channel.recv_exit_status()
                self.dump_data()
        return self.state

    def process_rtn(self):
        """ remote process exit code """
        return self.procrtn

    def process_terminate(self):
        """ terminate remote processes """
        if not self.stdout.channel.exit_status_ready():
            # process terminate()
            self.client.close()
            self.state = "terminate"
            self.procrtn = -1
            with open(self.logfileerr, mode='a') as errfile:
                errfile.write("Command did not complete within timeout.")

        return self.procrtn

    def match_testName(self):
        """ match the name of the log to the testcase """
        subtest_results_file = os.path.join(self.test_config['log_base_path'],
                                            "subtest_results.yml")
        if not os.path.exists(subtest_results_file):
            return
        with open(subtest_results_file, 'r') as fd:
            subtest_results_data = load(fd, Loader=Loader)
        test_set_name = str(subtest_results_data[0]['name'])

        for logfile in [self.logfileout, self.logfileerr]:
            (path, name) = os.path.split(logfile)
            namePlus = name.split('.')
            log_type = "console_{!s}".format(namePlus[1])
            new_name = os.path.join(path,
                                    ("{}.{}.{}.{}.log".format(
                                        test_set_name, test_set_name,
                                        log_type, self.node
                                        )))
            os.rename(logfile, new_name)


    def dump_files(self):
        """ dump the log files """
        with open(self.logfileout, mode='r') as fd:
            print("STDOUT: %s" % fd.read())
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

        copyList = self.test_directives.get('copyHostList', "no")
        if copyList == "yes":
            self.test_config['host_list'] = self.info.get_config('host_list')
        build_path = self.info.get_config('build_path')
        if not build_path:
            build_path = os.path.dirname(os.getcwd())
        self.test_config['build_path'] = build_path
        self.test_config['log_base_path'] = logdir
        self.test_config['node'] = self.node
        self.test_config['node_type'] = self.node_type
        self.test_config['hostlist'] = \
            ",".join(self.info.get_config(keyname='host_list'))
        if setFromConfig:
            self.test_config['setKeyFromConfig'] = {}
            self.test_config['setKeyFromConfig'].update(setFromConfig)

        # add testing directives
        self.test_config['setDirectiveFromConfig'] = {}
        if 'useKeyFile' in self.test_directives:
            self.test_config['setDirectiveFromConfig']['useKeyFile'] = \
                self.test_directives.get('useKeyFile')
        if directives:
            self.test_config['setDirectiveFromConfig'].update(directives)
        self.test_config['setDirectiveFromConfig']['renameTestRun'] = "no"
        self.logger.debug("conf: " + str(self.test_config))
