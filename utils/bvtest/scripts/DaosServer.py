#!/usr/bin/env python3
# Copyright (c) 2017 Intel Corporation
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
Python wrapper to launch the daos server on some number of nodes.

"""

import os
import socket
import logging
import subprocess
import shlex
import getpass
import tempfile
import io

class DaosServer(object):

    """ Background process that launches the DAOS server on some number of nodes. """

    def __init__(self, dir_path, test_info, node_control):
        self.dir_path = dir_path
        self.test_info = test_info
        self.node_control = node_control
        self.logger = logging.getLogger("TestRunnerLogger")
        self.proc = None

        if not os.path.exists("/tmp/hostfile"):
            with open("/tmp/hostfile", mode='w') as hostfile:
               hostfile.write(socket.gethostname())
               hostfile.flush()

        if os.path.exists("/mnt/daos"):
            os.system("sudo mount -t tmpfs -o size=16g tmpfs /mnt/daos")

    def setup_env(self):
        """setup environment variablies"""

        envlist = []
        envlist.append(' -x LD_LIBRARY_PATH={!s}'.format(
            self.test_info.get_defaultENV('LD_LIBRARY_PATH')))
        envlist.append(' -x CRT_PHY_ADDR_STR={!s}'.format(
            self.test_info.get_defaultENV('CRT_PHY_ADDR_STR', "ofi+sockets")))
        envlist.append(' -x DD_LOG={!s}'.format(
            self.test_info.get_defaultENV('DD_LOG')))
        envlist.append(' -x ABT_ENV_MAX_NUM_XSTREAMS={!s}'.format(
            self.test_info.get_defaultENV('ABT_ENV_MAX_NUM_XSTREAMS')))
        envlist.append(' -x ABT_MAX_NUM_XSTREAMS={!s}'.format(
            self.test_info.get_defaultENV('ABT_MAX_NUM_XSTREAMS')))
        envlist.append(' -x PATH={!s}'.format(
            self.test_info.get_defaultENV('PATH')))
        envlist.append(' -x OFI_PORT={!s}'.format(
            self.test_info.get_defaultENV('OFI_PORT')))
        envlist.append(' -x OFI_INTERFACE={!s}'.format(
            self.test_info.get_defaultENV('OFI_INTERFACE', "ib0")))
        return envlist

    def launch_process(self):

        """Launch DAOS server."""
        self.logger.info("Server: Launch the DAOS Server")

        envlist = self.setup_env()
        self.proc = None

        # hard-coded for now, but likely to become test parameters
        server_count = 1
        core_count = 1
        host_file = '/tmp/hostfile'
        uri_file = '/tmp/urifile'

        base_dir = self.test_info.get_defaultENV('PREFIX', '')
        ort_dir = self.test_info.get_defaultENV('ORT_PATH', '')

        server_cmd_list = []
        server_cmd_list.append("{0}/orterun --np {1} ".format(
            ort_dir, server_count))
        server_cmd_list.append("--c {0} --hostfile {1} --enable-recovery ".format(
            core_count, host_file))
        server_cmd_list.append("--report-uri {0} ".format(uri_file))
        server_cmd_list += envlist
        server_cmd_list.append(" {0}/bin/daos_server -g daos_server".format(base_dir))
        server_cmd = ''.join(server_cmd_list)
        cmdarg = shlex.split(server_cmd)

        self.logger.info("<DAOS Server> Server launch string: %s", server_cmd)
        logfileout = os.path.join(self.dir_path, "daos_server.out")
        logfileerr = os.path.join(self.dir_path, "daos_server.out")

        with open(logfileout, mode='w') as outfile, \
             open(logfileerr, mode='w') as errfile:

            outfile.write("Server launch string: {0}\n".format(server_cmd))
            outfile.flush()
            self.proc = subprocess.Popen(cmdarg,
                                     stdin=subprocess.DEVNULL,
                                     stdout=outfile,
                                     stderr=errfile)
            outfile.write("<DAOS Server> Server launched\n")
            outfile.flush()
        return 0

    def stop_process(self):
        """ Wait for processes to terminate and terminate them after
        the wait period. """

        self.logger.info("<DAOS Server> stopping processes :%s", self.proc.pid)

        self.proc.poll()
        rc = self.proc.returncode
        if rc is None:
            rc = -1
            try:
                self.proc.terminate()
                self.proc.wait(2)
                rc = self.proc.returncode
            except ProcessLookupError:
                pass
            except Exception:
                self.logger.error("Killing processes: %s", self.proc.pid)
                self.proc.kill()

        self.logger.info("<DAOS Server> - return code: %s\n", rc)

        # Always return success for now
        return 0
