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
import resource
import socket
import logging
import subprocess
import shlex
import getpass
import tempfile
import io
import time
import os
from paramiko import client

hostcount = 0
hostfilepath = ""
logpath = ""

class DaosServer(object):

    """ Background process that launches DAOS server on some number of nodes."""

    def __init__(self, dir_path, test_info, node_control):
        global hostcount
        global hostfilepath
        global logpath

        self.dir_path = dir_path
        self.test_info = test_info
        self.node_control = node_control
        self.logger = logging.getLogger("TestRunnerLogger")
        self.proc = None

        self.hostlist = test_info.get_subList('hostlist').split(',')
        hostcount = len(self.hostlist)

        hostfilepath = os.path.join(self.dir_path, "hostfile")
        daos_test_dir = test_info.get_defaultENV("DAOS_TEST_DIR",
                                                 "/scratch/daostest")
        if not os.path.exists(daos_test_dir):
            os.makedirs(daos_test_dir)
        self.urifilepath  = os.path.join(daos_test_dir, "urifile")
        logpath = os.path.join(self.dir_path, "daos_server.log")

        if os.path.exists(hostfilepath):
            os.remove(hostfilepath)
        with open(hostfilepath, mode='w') as hostfile:
                for host in self.hostlist:
                    hostfile.write(host)
                    hostfile.write(' slots=1\n')
                hostfile.flush()
        for host in self.hostlist:
            sshclient = client.SSHClient()
            sshclient.set_missing_host_key_policy(client.AutoAddPolicy())
            sshclient.connect(host)
            stdin, stdout, stderr = sshclient.exec_command(
                "sudo mount -t tmpfs -o size=16g tmpfs /mnt/daos")
            while not stdout.channel.exit_status_ready():
                if stdout.channel.recv_ready():
                    alldata = stdout.channel.recv(1024)
                    while stdout.channel.recv_ready():
                        alldata += stdout.channel.recv(1024)
                    print(str(alldata, "utf8", "backslashreplace"))
            stdin, stdout, stderr = sshclient.exec_command(
                "find /mnt/daos -maxdepth 1 -print0 | xargs -0r rm -rf")
            while not stdout.channel.exit_status_ready():
                if stdout.channel.recv_ready():
                    alldata = stdout.channel.recv(1024)
                    while stdout.channel.recv_ready():
                        alldata += stdout.channel.recv(1024)
                    print(str(alldata, "utf8", "backslashreplace"))

    def setup_env(self):
        """setup environment variablies"""

        global logpath

        envlist = []
        envlist.append(' -x LD_LIBRARY_PATH={!s}'.format(
            self.test_info.get_defaultENV('LD_LIBRARY_PATH')))
        envlist.append(' -x CRT_PHY_ADDR_STR={!s}'.format(
            self.test_info.get_defaultENV('CRT_PHY_ADDR_STR', "ofi+sockets")))
        envlist.append(' -x D_LOG_FILE={!s}'.format(logpath))
        envlist.append(' -x DD_SUBSYS=all')
        envlist.append(' -x DD_MASK=all')
        envlist.append(' -x ABT_ENV_MAX_NUM_XSTREAMS={!s}'.format(
            self.test_info.get_defaultENV('ABT_ENV_MAX_NUM_XSTREAMS')))
        envlist.append(' -x ABT_MAX_NUM_XSTREAMS={!s}'.format(
            self.test_info.get_defaultENV('ABT_MAX_NUM_XSTREAMS')))
        envlist.append(' -x PATH={!s}'.format(
            self.test_info.get_defaultENV('PATH')))
        envlist.append(' -x OFI_PORT={!s}'.format(
            self.test_info.get_defaultENV('OFI_PORT')))
        envlist.append(' -x OFI_INTERFACE={!s}'.format(
            self.test_info.get_defaultENV('OFI_INTERFACE', "eth0")))
        return envlist

    def launch_process(self):
        """Launch DAOS server."""
        global hostcount
        global hostfilepath

        self.logger.info("Server: Launch the DAOS Server")

        envlist = self.setup_env()
        self.proc = None

        # hard-coded for now, but likely to become test parameters
        thread_count = 1

        base_dir = self.test_info.get_defaultENV('PREFIX', '')
        ort_dir = self.test_info.get_defaultENV('ORT_PATH', '')

        server_cmd_list = []
        server_cmd_list.append("{0}/orterun --np {1} ".format(
            ort_dir, hostcount))
        server_cmd_list.append("--hostfile {0} --enable-recovery ".format(
            hostfilepath))
        server_cmd_list.append("--report-uri {0} ".format(self.urifilepath))
        server_cmd_list += envlist
        server_cmd_list.append(" {0}/bin/daos_server -d /tmp/.daos -g daos_server -c {1}".
                               format(base_dir, thread_count))
        server_cmd = ''.join(server_cmd_list)
        cmdarg = shlex.split(server_cmd)

        self.logger.info("<DAOS Server> Server launch string: %s", server_cmd)
        logfileout = os.path.join(self.dir_path, "daos_server.out")
        logfileerr = os.path.join(self.dir_path, "daos_server.out")

        """ Allow to get core files """
        try:
            resource.setrlimit(resource.RLIMIT_CORE,
                               (resource.RLIM_INFINITY, resource.RLIM_INFINITY))
        except (ValueError, resource.error):
            print("Unable to set infinite corefile limit")

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

    def dump_leftovers(self, hosts):
        """
        If there is something left running or data left dump out info on it.
        """
        dump_cmds = ["ps -ef | grep daos",
                     "ls -l /mnt/daos | tail -n +2"]

        for host in hosts:
            try:
                # look for left over daos processes
                outstr = subprocess.check_output("ssh {0} {1}".format(host, dump_cmds[0]),
                                                 shell=True)

                outlist = outstr.splitlines()
                for line in outlist:
                    self.logger.error(">>>>>DAOS process wasn't killed on %s: %s", host, line)
            except:
                pass

            try:
                # look for stuff in tmpfs
                outstr = subprocess.check_output("ssh {0} {1}".format(host, dump_cmds[1]),
                                                 shell=True)

                outlist = outstr.splitlines()
                for line in outlist:
                    self.logger.error(">>>>>Leftover cruft in tmpfs on %s: %s", host, line)
            except:
                pass


    def kill_server(self, hosts):
        """
        Sometimes stop doesn't get everything.  Really whack everything
        with this.

        hosts -- list of host names where servers are running
        """
        kill_cmds = ["pkill '(daos_server|daos_io_server)' --signal INT",
                     "sleep 5",
                     "pkill '(daos_server|daos_io_server)' --signal KILL"]
        rm_cmd = "find /mnt/daos -maxdepth 1 -print0 | xargs -0r rm -rf"

        for host in hosts:
            subprocess.call("ssh {0} \"{1}\"".format(host, '; '.join(kill_cmds)), shell=True)
            subprocess.call("ssh {0} \"{1}\"".format(host, rm_cmd), shell=True)

    def stop_process(self):
        """ Wait for processes to terminate and terminate them after
        the wait period. """

        self.logger.info("<DAOS Server> stopping processes :%s", self.proc.pid)

        self.proc.poll()
        rc = self.proc.returncode
        if rc is None:
            self.logger.info("Server is still running")
            try:
                self.proc.terminate()
                self.proc.wait(2)
                rc = self.proc.returncode
            except ProcessLookupError:
                pass
            except Exception:
                self.logger.error("Killing processes: %s", self.proc.pid)
                self.proc.kill()

        # now just double check and blow away anything that is left
        self.kill_server(self.hostlist)

        self.dump_leftovers(self.hostlist)

        # Always return success for now
        return 0
