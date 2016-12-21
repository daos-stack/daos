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
multi test runner class

"""

#pylint: disable=unused-import
#pylint: disable=too-many-instance-attributes
#pylint: disable=too-many-locals
#pylint: disable=too-many-public-methods

import os
import sys
import shutil
import logging
import time
from datetime import datetime
from importlib import import_module
#pylint: disable=import-error
import NodeControlRunner
import TestInfoRunner
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
    test_list = []
    subtest_results = []
    test_directives = {}
    info = None
    test_info = None
    nodes = None
    daemon = None
    logger = None

    def __init__(self, info, test_list=None):
        self.info = info
        self.test_list = test_list
        self.log_dir_base = self.info.get_config('log_base_path')
        self.logger = logging.getLogger("TestRunnerLogger")

    def dump_subtest_results(self):
        """ dump the test results to the log directory """
        if os.path.exists(self.log_dir_base):
            name = "%s/subtest_results.yml" % self.log_dir_base
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
        if self.daemon:
            self.daemon.stop_process()
        self.test_info.dump_test_info()
        self.dump_subtest_results()
        self.rename_output_directory()
        self.logger.info("TestRunner: tearDown end\n\n")

    def execute_strategy(self):
        """ execute test strategy """

        info = {}
        setConfigKeys = {}
        configKeys = {}
        rtn = 0
        info['name'] = self.test_info.get_test_info('module', 'name')
        toexit = self.test_directives.get('exitLoopOnError', "yes")
        for item in self.test_info.get_test_info('execStrategy'):
            configKeys.clear()
            setNodeType = item.get('nodeType', "all")
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
                    InfoKeys = self.test_info.get_test_info(loadFromInfo)
                    configKeys.update(InfoKeys)
            self.nodes.nodes_config(item['name'], setNodeType, configKeys)
            start_time = time.time()
            rtn = self.nodes.execute_list(setNodeType)
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

    def import_daemon(self, logbase):
        """ import the daemon module and load the class """
        _class = None
        name = self.test_info.get_test_info('use_daemon', 'name')
        logDir = os.path.join(logbase, name)
        try:
            os.makedirs(logDir)
        except OSError:
            newname = "%s_%s" % (logDir, datetime.now().isoformat())
            os.rename(logDir, newname)
            os.makedirs(logDir)
        try:
            _module = import_module(name)
            try:
                _class = getattr(_module, name)(logDir, self.test_info,
                                                self.nodes)
            except AttributeError:
                print("Class does not exist")
        except ImportError:
            print("Module does not exist")
        return _class

    def run_testcases(self):
        """ execute test scripts """

        file_hdlr = None
        sys.path.append("scripts")
        rtn = 0
        self.logger.info(
            "\n*************************************************************")
        test_module_name = self.test_list[0]
        self.test_info = TestInfoRunner.TestInfoRunner(self.info)
        if self.test_info.load_testcases(test_module_name):
            return 1
        logdir = os.path.join(self.log_dir_base, \
                              str(self.test_info.get_test_info(
                                  'module', 'name')))
        try:
            os.makedirs(logdir)
        except OSError:
            newname = "%s_%s" % (logdir, datetime.now().isoformat())
            os.rename(logdir, newname)
            os.makedirs(logdir)
        #value = logdir + "/multi.log"
        file_hdlr = logging.FileHandler(logdir + "/multi.log")
        self.logger.addHandler(file_hdlr)
        file_hdlr.setLevel(logging.DEBUG)
        self.test_directives = self.test_info.get_test_info('directives', {})
        self.test_info.add_default_env()
        #self.setup_default_env()
        self.logger.info("***************** " + \
                         str(self.test_info.get_test_info('module', 'name')) + \
                         " *********************************"
                        )
        # FIXME
        self.nodes = NodeControlRunner.NodeControlRunner(
            logdir, self.info, self.test_info)
        self.nodes.nodes_strategy(self.test_directives)
        if self.test_info.get_test_info('use_daemon'):
            self.daemon = self.import_daemon(logdir)
            rtn = self.daemon.launch_process()
        if not rtn:
            rtn_info = self.execute_strategy()
            rtn |= rtn_info['return_code']
            self.subtest_results.append(rtn_info)
        self.post_run()
        self.nodes.nodes_dump()
        file_hdlr.close()
        self.logger.removeHandler(file_hdlr)

        return rtn
