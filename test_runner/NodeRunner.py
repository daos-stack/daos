#!/usr/bin/env python
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

""" execute a command on a node """

#pylint: disable=too-many-instance-attributes
#pylint: disable=too-many-arguments

import os
import logging
import time
#pylint: disable=import-error
import paramiko
#pylint: enable=import-error


#pylint: disable=too-few-public-methods
class RetVal:
    """
    Placeholder standard return value class
    """
    def __init__(self, retcode=-1, running=False):
        self.running = running
        self.retcode = retcode
        self.data = None
#pylint: enable=too-few-public-methods


class NodeCmdRunner:
    """Simple node object"""

    def __init__(self, log_path, node, client):
        self.node = node
        self.client = client
        self.logger = logging.getLogger("TestRunnerLogger")
        self.state = "init"
        self.stdout = None
        self.stderr = None
        self.procrtn = 0
        self.cmdfileout = os.path.join(log_path, ("cmd_%s.out" % node))
        self.cmdfileerr = os.path.join(log_path, ("cmd_%s.err" % node))

    def client_close(self):
        """ close the coennction and fds """
        self.stdout.close()
        self.stderr.close()
        self.client.close()

    def execute_cmd(self, cmd, args, wait=True, timeout=15, environ=None):
        """
        Run specified cmd+args on self.node
        """
        self.logger.info("TestNodeRunner: start command %s on %s",
                         cmd, self.node)
        if self.stdout is not None:
            self.stdout.close()
        if self.stderr is not None:
            self.stderr.close()
        self.procrtn = 0
        cmdstr = "{!s} {!s}".format(cmd, args)
        with open(self.cmdfileout, mode='a') as outfile, \
            open(self.cmdfileerr, mode='a') as errfile:
            outfile.write("{!s}\n  Command: {!s} \n{!s}\n".format(
                ("=" * 40), cmdstr, ("=" * 40)))
            errfile.write("{!s}\n  Command: {!s} \n{!s}\n".format(
                ("=" * 40), cmdstr, ("=" * 40)))

        dummy_stdin, self.stdout, self.stderr = \
            self.client.exec_command(cmdstr, timeout=timeout,
                                     environment=environ)
        dummy_stdin.close()
        self.state = "running"
        if wait:
            return self.wait_for_exit(timeout)
        return

    def dump_data(self, retval):
        """ dump the output to a file """
        with open(self.cmdfileout, mode='a') as outfile:
            retval.data = str(self.stdout.read().decode('utf8'))
            # keepends=True newlines
            for line in retval.data.splitlines(True):
                outfile.write(line)
            outfile.write(str("\n"))
        with open(self.cmdfileerr, mode='a') as errfile:
            if not retval.retcode:
                errfile.write("Command complete within timeout.\n")
            else:
                errfile.write("Command did not complete within timeout.")
            for line in self.stderr.readlines():
                errfile.write(line)

    def process_state(self):
        """ poll remote processes for state """
        if self.state == "running":
            if self.stdout.channel.exit_status_ready():
                self.state = "done"
                self.procrtn = self.stdout.channel.recv_exit_status()
                retval = RetVal()
                retval.retcode = 0
                self.dump_data(retval)
                del retval
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
            with open(self.cmdfileerr, mode='a') as errfile:
                errfile.write("Command did not complete within timeout.\n")
        return self.procrtn

    def wait_for_exit(self, timeout=15):
        """
        Wait for async process called in execute_cmd to exit
        """
        t_end = time.time() + timeout
        retval = RetVal()
        while time.time() < t_end:
            if self.stdout.channel.exit_status_ready():
                break
            time.sleep(1)
        if time.time() >= t_end:  # we timed out, process never exited
            retval.retcode = -1
            retval.running = True
            self.state = "timeout"
        else:
            retval.retcode = self.stdout.channel.recv_exit_status()
            self.state = "done"
        self.dump_data(retval)
        return retval


class NodeRunner:
    """Simple node controller """

    def __init__(self, info, node, node_type='all'):
        self.test_info = info
        self.node = node
        self.node_type = node_type
        self.logger = logging.getLogger("TestRunnerLogger")
        # self.username = pwd.getpwuid(os.getuid()).pw_name
        self.username = os.getlogin()

    def connect(self, log_path):
        """ connect to remote system """
        connect_args = {}
        connect_args['hostname'] = self.node
        connect_args['username'] = self.username
        if self.test_info.get_directives('useKeyFile'):
            connect_args['key_filename'] = \
                self.test_info.get_directives('useKeyFile')
        connect_args['allow_agent'] = False
        connect_args['look_for_keys'] = True
        connect_args['timeout'] = 15
        client = paramiko.SSHClient()
        client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
        client.connect(**connect_args)
        return NodeCmdRunner(log_path, self.node, client)

    @staticmethod
    def close_connection(nodeCmd):
        """ close remote connection """
        nodeCmd.client_close()
        del nodeCmd
