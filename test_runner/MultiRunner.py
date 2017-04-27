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
multi test runner class

"""

#pylint: disable=too-many-instance-attributes

import os
import sys
import logging
import time
from datetime import datetime
from importlib import import_module
#pylint: disable=import-error
import NodeControlRunner
import TestInfoRunner
import ResultsRunner
import PostRunner
#pylint: enable=import-error


class MultiRunner(PostRunner.PostRunner):
    """Simple test runner"""
    logdir = ""
    test_list = []
    test_directives = {}
    info = None
    test_info = None
    nodes = None
    daemon = None
    daemon_results = None
    logger = None

    def __init__(self, info, test_list=None):
        self.info = info
        self.test_list = test_list
        self.log_dir_base = self.info.get_config('log_base_path')
        self.logger = logging.getLogger("TestRunnerLogger")
        self.now = "_{}".format(datetime.now().isoformat().replace(':', '.'))

    def rename_output_directory(self):
        """ rename the output directory """
        if os.path.exists(self.logdir):
            rename = str(self.test_directives.get('renameTestRun',
                                                  "no")).lower()
            if rename == "no":
                newname = self.logdir
            else:
                if rename == "yes":
                    newname = "{}{}".format(self.logdir, self.now)
                else:
                    newdir = str(self.test_directives.get('renameTestRun'))
                    logdir = os.path.dirname(self.logdir)
                    newname = os.path.join(logdir, newdir)
                os.rename(self.logdir, newname)
            self.logger.info("TestRunner: test log directory\n %s", \
                             os.path.abspath(newname))

            dowhat = str(self.test_directives.get('printTestLogPath',
                                                  "no")).lower()
            if dowhat == "yes":
                self.top_logdir(newname)
            elif dowhat == "dump":
                self.top_logdir(newname, dumpLogs=True)

    def stop_daemon(self):
        """ post run processing """
        self.logger.info("TestRunner: tearDown daemon begin")
        self.daemon.stop_process()
        module_name = str(self.test_info.get_test_info('use_daemon', 'name'))
        logDir = os.path.join(self.logdir, module_name)
        self.check_log_mode(logDir)
        rtn_info = {'duration' : 0, 'return_code' : 0,
                    'status' : "PASS", 'name' : module_name,
                    'error' : ""}
        self.daemon_results.update_subtest_results(rtn_info)
        self.daemon_results.update_testset_results(status="PASS")
        self.test_logtopdir(self.daemon_results)
        self.daemon_results.create_test_set_results()
        del self.daemon
        del self.daemon_results
        self.logger.info("TestRunner: tearDown daemon end\n")

    def post_run(self, subtest_results):
        """ post run processing """
        self.logger.info("TestRunner: tearDown begin")
        if self.daemon:
            self.stop_daemon()
        subtest_results.create_test_set_results()
        self.test_info.dump_test_info()
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
        start_time = time.time()
        for item in self.test_info.get_test_info('execStrategy'):
            configKeys.clear()
            setConfigKeys = item.get('setConfigKeys', {})
            self.logger.debug("set_configKeys: %s", str(setConfigKeys))
            if setConfigKeys:
                configKeys = self.test_info.set_configKeys(setConfigKeys)
            setNodeType = item.get('nodeType', "all")
            setTestPhase = item.get('type', "TEST").upper()
            configKeys['TR_TEST_PHASE'] = setTestPhase
            self.nodes.nodes_config(item['name'], setNodeType, configKeys)
            thisrtn = self.nodes.execute_list(setNodeType)
            rtn |= thisrtn
            if thisrtn and toexit.lower() == "yes":
                break

        info['duration'] = '{:.2f}'.format(time.time() - start_time)
        info['return_code'] = rtn
        if rtn == 0:
            info['status'] = "PASS"
        else:
            info['status'] = "FAIL"
        info['error'] = ""

        return info

    def import_daemon(self, logbase):
        """ import the daemon module and load the class """
        _class = None
        name = self.test_info.get_test_info('use_daemon', 'name')
        logDir = os.path.join(logbase, name)
        self.daemon_results = ResultsRunner.SubTestResults(logDir, name)
        self.logger.info("Import daemon: %s", name)
        try:
            os.makedirs(logDir)
        except OSError:
            newname = "{}{}".format(logDir, self.now)
            os.rename(logDir, newname)
            os.makedirs(logDir)
        try:
            _module = import_module(name)
            try:
                _class = getattr(_module, name)(logDir, self.test_info,
                                                self.nodes)
            except AttributeError:
                self.logger.error("Class does not exist")
        except ImportError:
            self.logger.error("Module does not exist")
        return _class

    #pylint: disable=too-many-statements
    def run_testcases(self):
        """ execute test scripts """

        file_hdlr = None
        sys.path.append("scripts")
        run_rtn = 0
        main_file = logging.FileHandler(
            os.path.join(self.log_dir_base, "TestRunner.log"))
        main_file.setLevel(logging.DEBUG)
        self.logger.addHandler(main_file)
        self.logger.info(
            "\n**********************************************************")
        self.test_info = TestInfoRunner.TestInfoRunner(self.info)
        for test_module_name in self.test_list:
            rtn = 0
            if self.test_info.load_testcases(test_module_name, True):
                return 1
            module_name = str(self.test_info.get_test_info('testName'))
            results = ResultsRunner.SubTestResults(self.log_dir_base,
                                                   module_name)
            self.logdir = os.path.join(self.log_dir_base, module_name)
            try:
                os.makedirs(self.logdir)
            except OSError:
                self.logger.info("Rename directory: %s", self.logdir)
                newname = "{}{}".format(self.logdir, self.now)
                os.rename(self.logdir, newname)
                os.makedirs(self.logdir)
            file_hdlr = logging.FileHandler(os.path.join(self.logdir,
                                                         (module_name + ".log"))
                                           )
            self.logger.addHandler(file_hdlr)
            file_hdlr.setLevel(logging.DEBUG)
            self.test_directives = self.test_info.get_test_info('directives',
                                                                None, {})
            self.test_info.add_default_env()
            self.logger.info("***************** " + \
                             module_name + \
                             " *********************************"
                            )
            self.nodes = NodeControlRunner.NodeControlRunner(
                self.logdir, self.info, self.test_info)
            self.nodes.nodes_strategy(self.test_directives)
            rtn_info = None
            if self.test_info.get_test_info('use_daemon'):
                self.daemon = self.import_daemon(self.logdir)
                rtn = self.daemon.launch_process()
            if not rtn:
                rtn_info = self.execute_strategy()
                rtn |= int(rtn_info['return_code'])
            else:
                rtn_info = {'duration' : 0, 'return_code' : rtn,
                            'status' : "FAIL", 'name' : module_name,
                            'error' : "Daemon start failed"}

            results.update_subtest_results(rtn_info)
            results.update_testset_results(status=rtn_info['status'])

            self.post_run(results)
            file_hdlr.close()
            self.logger.removeHandler(file_hdlr)
            self.nodes.nodes_dump()
            del self.nodes
            self.nodes = None
            del self.test_directives
            self.test_info.cleanup_test_info()
            run_rtn |= rtn
        self.logger.info(
            "\n*************************************************************")
        main_file.close()
        self.logger.removeHandler(main_file)

        return run_rtn
