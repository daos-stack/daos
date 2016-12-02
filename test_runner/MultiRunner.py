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
test runner class

"""

#pylint: disable=unused-import
#pylint: disable=too-many-instance-attributes
#pylint: disable=too-many-locals
#pylint: disable=too-many-public-methods

import os
import sys
import shutil
import unittest
import logging
import time
from datetime import datetime
#pylint: disable=import-error
import NodeRunner
import PostRunner
#pylint: enable=import-error

from yaml import load, dump
try:
    from yaml import CLoader as Loader, CDumper as Dumper
except ImportError:
    from yaml import Loader, Dumper


class MultiRunner(PostRunner.PostRunner):
    """Simple test runner"""
    log_dir_base = ""
    test_info = {}
    test_list = []
    node_list = []
    subtest_results = []
    test_directives = {}
    info = None
    logger = None
    file_hdlr = None

    def __init__(self, info, test_list=None):
        self.info = info
        self.test_list = test_list
        self.log_dir_base = self.info.get_config('log_base_path')
        self.logger = logging.getLogger("TestRunnerLogger")
        self.logger.setLevel(logging.DEBUG)
        ch = logging.StreamHandler(sys.stdout)
        self.logger.addHandler(ch)

    def dump_subtest_results(self):
        """ dump the test results to the log directory """
        log_dir = os.path.dirname(self.info.get_config('log_base_path'))
        if os.path.exists(log_dir):
            name = "%s/subtest_results.yml" % log_dir
            with open(name, 'w') as fd:
                dump(self.subtest_results, fd, Dumper=Dumper, indent=4,
                     default_flow_style=False)

    def rename_output_directory(self):
        """ rename the output directory """
        if os.path.exists(self.log_dir_base):
            rename = str(self.test_directives.get('renameTestRun',
                                                  "yes")).lower()
            if rename == "no":
                newname = self.log_dir_base
            else:
                if rename == "yes":
                    newname = "%s_%s" % \
                              (self.log_dir_base,
                               datetime.now().isoformat().replace(':', '.'))
                else:
                    newdir = str(self.test_directives.get('renameTestRun'))
                    logdir = os.path.dirname(self.log_dir_base)
                    newname = os.path.join(logdir, newdir)
                os.rename(self.log_dir_base, newname)
                self.logger.info("TestRunner: test log directory\n %s", \
                                 os.path.abspath(newname))

            dowhat = str(self.test_directives.get('printTestLogPath',
                                                  "no")).lower()
            if dowhat == "yes":
                self.top_logdir(newname)
            elif dowhat == "dump":
                self.top_logdir(newname, dumpLogs=True)

    def post_run(self):
        """ post run processing """
        self.logger.info("TestRunner: tearDown begin")
        self.dump_subtest_results()
        self.rename_output_directory()
        self.logger.info("TestRunner: tearDown end\n\n")

    def execute_list(self):
        """ execute test scripts """

        rtn = 0
        for node in self.node_list:
            self.logger.info("************** run " + \
                             str(node.test_name) + " on " + str(node.node) + \
                             " ******************************"
                            )
            node.launch_test()
        loop_count = 720
        running_count = len(self.node_list)
        self.logger.info("******* started running count " + str(running_count))
        while running_count and loop_count:
            time.sleep(1)
            running_count = len(self.node_list)
            loop_count = loop_count - 1
            for node in self.node_list:
                if node.process_state() is not "running":
                    running_count = running_count - 1
        self.logger.info("******* done running count " + str(running_count))

        for node in self.node_list:
            procrtn = node.process_rtn()
            if procrtn is not None:
                rtn |= procrtn
            else:
                node.process_terminate()
                rtn |= 1
            self.logger.info("***************** Results " + \
                             "*********************************"
                            )
            self.logger.info(str(node.test_name) + " on " + \
                             str(node.node) + " rtn: " +  str(procrtn)
                            )

            self.logger.info(
                "***********************************************************")

        return rtn

    def execute_strategy(self):
        """ execute test strategy """

        info = {}
        setConfigKeys = {}
        configKeys = {}
        rtn = 0
        info['name'] = self.test_info['module']['name']
        value = self.log_dir_base + "/" + str(info['name'])
        start_time = time.time()
        toexit = self.test_directives.get('exitLoopOnError', "yes")
        for item in self.test_info['execStrategy']:
            configKeys.clear()
            setConfigKeys = item.get('setConfigKeys', {})
            setTestPhase = item.get('type', "TEST").upper()
            configKeys['TR_TEST_PHASE'] = setTestPhase
            if setConfigKeys:
                loadFromConfig = setConfigKeys.get('loadFromConfig', "")
                if loadFromConfig:
                    addKeys = self.info.get_config(loadFromConfig)
                    configKeys.update(addKeys)
                loadFromInfo = setConfigKeys.get('loadFromInfo', "")
                if loadFromInfo:
                    InfoKeys = self.test_info.get(loadFromInfo)
                    configKeys.update(InfoKeys)
            for node in self.node_list:
                logdir = value + "/" + item['name'] + "_" + node.node
                os.makedirs(logdir)
                self.logger.info("setup node " + str(node.node) + " " + \
                                  str(logdir))
                node.setup_config(item['name'], logdir, configKeys)
            rtn = self.execute_list()
            info['duration'] = time.time() - start_time
            info['return_code'] = rtn
            if rtn == 0:
                info['status'] = "PASS"
            else:
                info['status'] = "FAIL"
            info['error'] = ""
            if rtn and toexit.lower() == "yes":
                break

        return info

    def node_strategy(self):
        """ execute test strategy """
        self.logger.info("***************** " + \
                         str(self.test_info['module']['name']) + \
                         " *********************************"
                        )
        path = os.getcwd()
        scripts = path + "/scripts"
        host_list = self.info.get_config('host_list')
        self.logger.info("host_list" + str(host_list))
        for node in host_list:
            node_info = NodeRunner.NodeRunner(self.info, node, path, scripts,
                                              self.test_directives)
            self.node_list.append(node_info)
        return self.execute_strategy()

    def load_testcases(self, test_module_name):
        """ load and check test description file """

        rtn = 0
        with open(test_module_name, 'r') as fd:
            self.test_info = load(fd, Loader=Loader)

        if 'description' not in self.test_info:
            self.logger.error(" No description defined in file: %s", \
                             test_module_name)
            rtn = 1
        if 'module' not in self.test_info:
            self.logger.error(" No module section defined in file: %s", \
                             test_module_name)
            rtn = 1
        if 'directives' not in self.test_info or \
           self.test_info['directives'] is None:
            self.test_info['directives'] = {}
        if 'execStrategy' not in self.test_info:
            self.logger.error(" No execStrategy section defined in file: %s",
                              test_module_name)
            rtn = 1
        return rtn

    def run_testcases(self):
        """ execute test scripts """

        sys.path.append("scripts")
        rtn = 0
        self.logger.info(
            "\n*************************************************************")
        test_module_name = self.test_list[0]
        self.test_info.clear()
        if self.load_testcases(test_module_name):
            return 1
        logdir = os.path.join(self.log_dir_base, \
                              str(self.test_info['module']['name']))
        try:
            os.makedirs(logdir)
        except OSError:
            newname = "%s_%s" % (logdir, datetime.now().isoformat())
            os.rename(logdir, newname)
            os.makedirs(logdir)
        #value = logdir + "/multi.log"
        file_hdlr = logging.FileHandler(logdir + "/multi.log")
        self.logger.addHandler(file_hdlr)
        self.test_directives = self.test_info.get('directives', {})
        rtn_info = self.node_strategy()
        rtn |= rtn_info['return_code']
        self.subtest_results.append(rtn_info)
        self.post_run()
        file_hdlr.close()
        self.logger.removeHandler(file_hdlr)

        return rtn
