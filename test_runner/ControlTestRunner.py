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
node control test runner class

This class is used by Multi Runner to start a copy of Test Runner on remote
nodes. The class uses the nodes type classification to determine which nodes
to execute the request on. The class uses the nodes_strategy method to create
a node object of each node and assign it to a type.

To execute on a remote node:
The class uses the nodes_config method to setup the data to be use for the
config file and define the description file to be executed on each node.

The execute_list method is the main entry point to start the execution. This
method will create a list or sub-list of node of a type execute on. The method
will query each node to match the requested type. This list can be broken down
into a sub-list by the call suppling a comma  separate list on index into the
list. This list of node is passed to execute_config to start Test Runner on all
listed node and wait for completion. At the completion of the request, the
match_name method is called on all nodes to rename the console log file to
match the test set name of the completed request.

"""


import os
import logging
import time
from datetime import datetime
#pylint: disable=import-error
import RemoteTestRunner
#pylint: enable=import-error


class ControlTestRunner():
    """Simple test runner"""
    node_list = []

    def __init__(self, log_base, info, test_info):
        self.info = info
        self.test_info = test_info
        self.log_dir_base = log_base
        self.logger = logging.getLogger("TestRunnerLogger")
        self.now = "_{}".format(datetime.now().isoformat().replace(':', '.'))

    def execute_config(self, run_node_list, waittime):
        """ execute test scripts """
        rtn = 0
        for node in run_node_list:
            node.launch_test()
        loop_count = waittime
        running_count = len(run_node_list)
        self.logger.debug("******* started running count " + str(running_count))
        while running_count and loop_count:
            time.sleep(1)
            running_count = len(run_node_list)
            loop_count = loop_count - 1
            for node in run_node_list:
                if node.process_state() != "running":
                    running_count = running_count - 1
        self.logger.debug("******* done running count " + str(running_count))
        self.logger.info("********** node Test Runner Results **************")
        for node in run_node_list:
            procrtn = node.process_rtn()
            if procrtn is not None:
                rtn |= procrtn
            else:
                node.process_terminate()
                rtn |= 1
            if node.test_name:
                self.logger.info(str(node.test_name) + " on " + \
                                 str(node.node) + " rtn: " +  str(procrtn))

        self.logger.info("execution time remaining: %d", loop_count)
        self.logger.info(
            "***********************************************************")
        for node in run_node_list:
            node.match_testName()

        return rtn

    def execute_list(self, node_type="all", nodes="all", waittime=1800):
        """ create the execute test list
            node_type - is a define type of nodes
            nodes - s a list of indexs into the type list """
        run_node_list = []
        self.logger.info("********** run Test Runner on node type " + \
                         str(node_type) + \
                         " **********"
                        )
        if nodes != "all":
            node_index_list = nodes.split(",")
            if node_type == "all":
                type_list = self.info.get_config('host_list')
            else:
                type_list = self.test_info.get_defaultENV(node_type).split(",")
        for node in self.node_list:
            if node.match_type(node_type):
                if nodes == "all":
                    run_node_list.append(node)
                else:
                    for index in node_index_list:
                        if node.node == type_list[int(index)]:
                            run_node_list.append(node)
                            break
        return self.execute_config(run_node_list, waittime)

    def nodes_config(self, name, node_type, configKeys):
        """ setup the node config file """

        addKeys = self.test_info.get_passToConfig()
        if addKeys:
            configKeys.update(addKeys)
        value = self.log_dir_base + "/" + str(name)
        for node in self.node_list:
            logdir = value + "/" + name + "_" + node.node
            try:
                os.makedirs(logdir)
            except OSError:
                newname = "{}{}".format(logdir, self.now)
                os.rename(logdir, newname)
                os.makedirs(logdir)

            self.logger.debug("setup node " + str(node.node) + " " + \
                              str(logdir))
            node.setup_config(name, logdir, node_type, configKeys)

    def nodes_strategy(self, test_directives):
        """ create node objects """
        path = os.getcwd()
        scripts = os.path.join(path, "scripts")
        hostKeys = self.test_info.get_test_info('module', 'setKeyFromHost')
        if hostKeys:
            for item in hostKeys:
                for node in self.test_info.get_defaultENV(item).split(","):
                    node_info = RemoteTestRunner.RemoteTestRunner(
                        self.info, node, path, scripts, test_directives,
                        str(item))
                    self.node_list.append(node_info)
        else:
            for node in self.info.get_config('host_list'):
                node_info = RemoteTestRunner.RemoteTestRunner(
                    self.info, node, path, scripts, test_directives, "all")
                self.node_list.append(node_info)

    def nodes_dump(self):
        """ dump node objects """
        for node in self.node_list:
            node.dump_info()
            del node
        del self.node_list[:]
