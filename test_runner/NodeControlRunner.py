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
node control test runner class

"""


import os
import logging
import time
#pylint: disable=import-error
import NodeRunner
#pylint: enable=import-error


class NodeControlRunner():
    """Simple test runner"""
    #log_dir_base = ""
    node_list = []
    info = None
    test_info = None
    logger = None
    file_hdlr = None

    def __init__(self, log_base, info, test_info=None):
        self.info = info
        self.test_info = test_info
        self.log_dir_base = log_base
        self.logger = logging.getLogger("TestRunnerLogger")

    def execute_cmd(self, cmdstr, log_path, node_type, msg=None):
        """ execute test scripts """

        rtn = 0
        self.logger.info(
            "***********************************************************")
        if msg:
            self.logger.info("%s", msg)
        for node in self.node_list:
            self.logger.debug("************** run commnad on " + \
                             str(node.node) + " type " + str(node_type) + \
                             " **************"
                             )
            node.run_cmd(cmdstr, log_path, node_type)
        loop_count = 720
        running_count = len(self.node_list)
        self.logger.debug("******* started running count " + str(running_count))
        while running_count and loop_count:
            time.sleep(1)
            running_count = len(self.node_list)
            loop_count = loop_count - 1
            for node in self.node_list:
                if node.process_state() != "running":
                    running_count = running_count - 1
        self.logger.debug("******* done running count " + str(running_count))

        for node in self.node_list:
            procrtn = node.process_rtn()
            if procrtn is not None:
                rtn |= procrtn
            else:
                node.process_terminate()
                rtn |= 1
            self.logger.debug("************** Results " + \
                             str(node.node) + " rtn: " +  str(procrtn) + \
                             " **************"
                             )
        self.logger.info(
            "***********************************************************")
        return rtn

    def execute_list(self, node_type):
        """ execute test scripts """

        rtn = 0
        self.logger.info("********** run Test Runner on node type " + \
                         str(node_type) + \
                         " **********"
                        )
        for node in self.node_list:
            node.launch_test(node_type)
        loop_count = 720
        running_count = len(self.node_list)
        self.logger.debug("******* started running count " + str(running_count))
        while running_count and loop_count:
            time.sleep(1)
            running_count = len(self.node_list)
            loop_count = loop_count - 1
            for node in self.node_list:
                if node.process_state() != "running":
                    running_count = running_count - 1
        self.logger.debug("******* done running count " + str(running_count))
        self.logger.info("********** node Test Runner Results **************")
        for node in self.node_list:
            procrtn = node.process_rtn()
            if procrtn is not None:
                rtn |= procrtn
            else:
                node.process_terminate()
                rtn |= 1
            if node.test_name:
                self.logger.info(str(node.test_name) + " on " + \
                                 str(node.node) + " rtn: " +  str(procrtn))

        self.logger.info(
            "***********************************************************")

        return rtn

    def nodes_config(self, name, node_type, configKeys):
        """ setup the node config file """

        addKeys = self.test_info.get_passToConfig()
        if addKeys:
            configKeys.update(addKeys)
        value = self.log_dir_base + "/" + str(name)
        for node in self.node_list:
            logdir = value + "/" + name + "_" + node.node
            os.makedirs(logdir)
            self.logger.debug("setup node " + str(node.node) + " " + \
                              str(logdir))
            node.setup_config(name, logdir, node_type, configKeys)

    def nodes_strategy(self, test_directives):
        """ create node objects """
        path = os.getcwd()
        scripts = path + "/scripts"
        host_list = self.info.get_config('host_list')
        self.logger.info("host_list" + str(host_list))
        hostKeys = self.test_info.get_test_info('module', 'setKeyFromHost')
        for node in host_list:
            node_type = 'all'
            if hostKeys:
                for item in hostKeys:
                    if node in self.test_info.get_defaultENV(item):
                        node_type = str(item)
            node_info = NodeRunner.NodeRunner(self.info, node, path, scripts,
                                              test_directives, node_type)
            self.node_list.append(node_info)

    def nodes_dump(self):
        """ dump node objects """
        for node in self.node_list:
            node.dump_info()
