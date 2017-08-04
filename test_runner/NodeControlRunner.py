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


import logging
import time
from datetime import datetime
#pylint: disable=import-error
import OrteRunner
import NodeRunner
#pylint: enable=import-error


class NodeControlRunner(OrteRunner.OrteRunner):
    """Simple node interface"""
    node_list = []

    def __init__(self, log_base, test_info):
        self.test_info = test_info
        self.info = test_info.info
        self.log_dir_base = log_base
        self.logger = logging.getLogger("TestRunnerLogger")
        self.now = "_{}".format(datetime.now().isoformat().replace(':', '.'))
        host_list = self.info.get_config('host_list')
        host_list = self.test_info.get_subList('hostlist').split(',')
        for node in host_list:
            node_info = NodeRunner.NodeRunner(self.test_info, node)
            self.node_list.append(node_info)

    def execute_list(self, cmdstr, log_path, run_node_list, waittime):
        """ execute command on node list """

        rtn = 0
        for node in run_node_list:
            self.logger.debug("%s  run commnad on : %s %s",
                              ("*" * 10), str(node.node), ("*" * 10))
            node.run_cmd(cmdstr, log_path)
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

    def find_node(self, testnode):
        """ find node object """
        for node in self.node_list:
            if node.node == testnode:
                return node
        return None

    #pylint: disable=too-many-arguments
    def execute_cmd(self, cmdstr, log_path, nodes="all", msg="", waittime=1800):
        """ create node list and execute command """

        rtn = 0
        run_node_list = []

        self.logger.info("%s", ("*" * 40))
        if msg:
            self.logger.info("%s", msg)
        if nodes == "all":
            run_node_list = self.node_list
        else:
            if nodes[0].isupper():
                node_list = self.test_info.get_defaultENV(nodes).split(",")
            else:
                node_list = nodes.split(",")
            for node in node_list:
                run_node_list.append(self.find_node(node))

        rtn = self.execute_list(cmdstr, log_path, run_node_list, waittime)
        return rtn

    #pylint: enable=too-many-arguments

    def nodes_dump(self):
        """ dump node objects """
        for node in self.node_list:
            node.dump_info()
            del node
        del self.node_list[:]

    #pylint: disable=too-many-arguments
    def paramiko_execute_remote_cmd(self, node, cmd, args, wait, timeout):
        """
        Pass through to NodeRunner:execute_cmd()
        """
        run_node = self.find_node(node)
        return run_node.execute_cmd(cmd, args, self.log_dir_base,
                                    wait=wait, timeout=timeout)
    #pylint: enable=too-many-arguments

    def paramiko_wait_for_exit(self, node, retval, timeout):
        """
        Pass through to NodeRunner:wait_for_exit()
        """
        run_node = self.find_node(node)
        return run_node.wait_for_exit(retval, timeout)
