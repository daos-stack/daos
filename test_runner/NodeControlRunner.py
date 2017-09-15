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
node control for test suite

A class to execute a command on one or more nodes. The node list can be
defined by node type or name. This class uses NodeRunner to execute the request.

"""


import os
import logging
import time
from datetime import datetime
#pylint: disable=import-error
import NodeRunner
import CmdRunner
import OrteRunner
#pylint: enable=import-error


class NodeControlRunner():
    """Simple node interface"""

    def __init__(self, log_base, test_info):
        self.test_info = test_info
        self.info = test_info.info
        self.log_dir_base = log_base
        self.logger = logging.getLogger("TestRunnerLogger")
        self.now = "_{}".format(datetime.now().isoformat().replace(':', '.'))
        host_list = self.test_info.get_subList('hostlist').split(',')
        self.node_list = []
        for node in host_list:
            node_info = NodeRunner.NodeRunner(self.test_info, node)
            self.node_list.append(node_info)

    @staticmethod
    def start_local_cmd(log_path, log_name="localcmd"):
        """ create an local command Test Runner object
            create the log directory """
        log_dir = os.path.abspath(log_path)
        try:
            os.makedirs(log_dir)
        except OSError:
            pass
        return CmdRunner.CmdRunner(log_dir, log_name)

    def start_cmd_list(self, log_path, testsuite, prefix):
        """ create an orte Test Runner object
            create the log directory """
        log_dir_orte = os.path.abspath(log_path)
        try:
            os.makedirs(log_dir_orte)
        except OSError:
            pass
        return (OrteRunner.OrteRunner(self.test_info, log_dir_orte,
                                      testsuite, prefix))

    def start_remote_cmd(self, nodeName, log_dir=""):
        """ Pass through to NodeRunner:connect """
        if not log_dir:
            log_dir = self.log_dir_base
        node = self.find_node(nodeName)
        return node.connect(log_dir)

    def close_remote_cmd(self, nodeCmd):
        """ Pass through to NodeRunner:close """
        node = self.find_node(nodeCmd.node)
        node.close_connection(nodeCmd)

    def find_node(self, testnode):
        """ find node object """
        for node in self.node_list:
            if node.node == testnode:
                return node
        return None

    def close_list(self, run_node_list):
        """ execute command on node list """
        self.logger.info("Close node connections")
        for nodeCmd in run_node_list:
            node = self.find_node(nodeCmd.node)
            node.close_connection(nodeCmd)

    def nodes_dump(self):
        """ dump node objects """
        for node in self.node_list:
            node.dump_info()
            del node
        del self.node_list[:]

    def create_remote_list(self, log_path, nodes="all", msg=""):
        """ create node list """
        run_node_list = []
        self.logger.info("%s", ("*" * 40))
        if msg:
            self.logger.info("%s", msg)
        if nodes == "all":
            for node in self.node_list:
                run_node_list.append(node.connect(log_path))
        else:
            if nodes[0].isupper():
                node_list = self.test_info.get_defaultENV(nodes).split(",")
            else:
                node_list = nodes.split(",")
            for node in node_list:
                run_node_list.append(self.start_remote_cmd(node, log_path))

        return run_node_list

    #pylint: disable=too-many-arguments
    def execute_list(self, cmdstr, run_node_list, waittime=180, environ=None):
        """ execute command on node list """

        rtn = 0
        for node in run_node_list:
            self.logger.debug("%s  run commnad on : %s %s",
                              ("*" * 10), str(node.node), ("*" * 10))
            node.execute_cmd(cmdstr, "", False, 10, environ)
        loop_count = waittime
        running_count = len(run_node_list)
        self.logger.debug("******* started running count %d", running_count)
        while running_count and loop_count:
            time.sleep(1)
            running_count = len(run_node_list)
            loop_count = loop_count - 1
            for node in run_node_list:
                if node.process_state() != "running":
                    running_count = running_count - 1
        self.logger.debug("******* done running count %d", running_count)

        for node in run_node_list:
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
        self.logger.info("execution time remaining: %d", loop_count)
        self.logger.info("%s", ("*" * 40))
        return rtn

    def execute_remote_cmd(self, cmdstr, log_path, nodes="all", msg="",
                           waittime=1800, environ=None):
        """ create node list and execute command """
        run_node_list = self.create_remote_list(log_path, nodes, msg)
        rtn = self.execute_list(cmdstr, run_node_list, waittime, environ)
        self.close_list(run_node_list)
        return rtn

    def paramiko_execute_remote_cmd(self, nodeName, cmd, args, wait=True,
                                    timeout=15, environ=None):
        """
        Pass through to NodeRunner:execute_cmd()
        """
        nodeCmd = self.start_remote_cmd(nodeName)
        return nodeCmd.execute_cmd(cmd, args, wait, timeout, environ)
    #pylint: enable=too-many-arguments

    #pylint: disable=unused-argument
    def paramiko_wait_for_exit(self, node, nodeCmd, timeout):
        """
        Pass through to NodeRunner:wait_for_exit()
        """
        retval = nodeCmd.wait_for_exit(timeout=15)
        self.close_remote_cmd(nodeCmd)
        return retval
