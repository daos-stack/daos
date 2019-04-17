#!/usr/bin/python
"""
  (C) Copyright 2018-2019 Intel Corporation.

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
  The Government's rights to use, modify, reproduce, release, perform, display,
  or disclose this software are subject to the terms of the Apache License as
  provided in Contract No. B609815.
  Any reproduction of computer software, computer software documentation, or
  portions thereof marked with this legend must also reproduce the markings.
"""

import os
import subprocess
from apricot import TestWithoutServers

import agent_utils
import server_utils
import write_host_file
from daos_api import DaosContext, DaosLog

class CartSelfTest(TestWithoutServers):
    """
    Runs a few variations of CaRT self-test to ensure network is in a
    stable state prior to testing.
    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        super(CartSelfTest, self).__init__(*args, **kwargs)

        self.self_test_bin = None
        self.endpoint = None
        self.max_rpcs = None
        self.repetitions = None
        self.message_size = None
        self.share_addr = None
        self.env_dict = None
        self.env_list = None

    # start servers, establish file locations, etc.
    def setUp(self):
        super(CartSelfTest, self).setUp()
        self.agent_sessions = None

        self.hostlist_servers = self.params.get("test_machines", '/run/hosts/')
        self.hostfile_servers = write_host_file.write_host_file(
            self.hostlist_servers, self.workdir)

        context = DaosContext(self.prefix + '/lib/')
        self.d_log = DaosLog(context)

        # self_test params
        self.self_test_bin = os.path.join(self.prefix, "bin/self_test")
        self.endpoint = self.params.get("endpoint", "/run/testparams/")
        self.max_rpcs = self.params.get("max_inflight_rpcs", "/run/testparams/")
        self.repetitions = self.params.get("repetitions", "/run/testparams/")
        self.message_size = (
            self.params.get("size", "/run/muxtestparams/message_size/*")[0])
        self.share_addr = self.params.get("val",
                                          "/run/muxtestparams/share_addr/*")[0]
        self.env_dict = {
            "CRT_PHY_ADDR_STR":     "ofi+sockets",
            "CRT_CTX_NUM":          "8",
            "OFI_INTERFACE":        "eth0",
            "CRT_CTX_SHARE_ADDR":   str(self.share_addr)
        }
        self.env_list = []
        for key, val in self.env_dict.items():
            self.env_list.append("-x")
            self.env_list.append("{0}={1}".format(key, val))

        # daos server params
        self.server_group = self.params.get("server", 'server_group',
                                            'daos_server')
        self.uri_file = os.path.join(self.basepath, "install", "tmp", "uri.txt")
        self.agent_sessions = agent_utils.run_agent(self.basepath,
                                                    self.hostlist_servers)
        server_utils.run_server(self.hostfile_servers, self.server_group,
                                self.basepath, uri_path=self.uri_file,
                                env_dict=self.env_dict)

    def tearDown(self):
        try:
            os.remove(self.hostfile_servers)
            os.remove(self.uri_file)
        finally:
            if self.agent_sessions:
                agent_utils.stop_agent(self.hostlist_servers,
                                       self.agent_sessions)
            server_utils.stop_server(hosts=self.hostlist_servers)
            super(CartSelfTest, self).tearDown()

    def test_self_test(self):
        """
        Run a few CaRT self-test scenarios

        :avocado: tags=network,small,quick,cartselftest
        """
        base_cmd = [self.orterun,
                    "-np", "1",
                    "-ompi-server", "file:{0}".format(self.uri_file)]
        selftest = [self.self_test_bin,
                    "--group-name", "{0}".format(self.server_group),
                    "--endpoint", "{0}".format(self.endpoint),
                    "--message-sizes", "{0}".format(self.message_size),
                    "--max-inflight-rpcs", "{0}".format(self.max_rpcs),
                    "--repetitions", "{0}".format(self.repetitions)]

        cmd = base_cmd + self.env_list + selftest

        cmd_log_str = ""
        for elem in cmd:
            cmd_log_str += elem + " "
        try:
            self.d_log.info("Running cmd {0}".format(cmd_log_str))
            subprocess.check_output(cmd, stderr=subprocess.STDOUT)
        except subprocess.CalledProcessError as exc:
            self.d_log.error("CaRT self_test returned non-zero. "
                             "rc {0}:".format(exc.returncode))
            for line in exc.output.split('\n'):
                self.d_log.error("{0}".format(line))
            self.fail("CaRT self_test returned non-zero")
