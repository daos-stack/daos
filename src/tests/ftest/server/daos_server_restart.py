#!/usr/bin/python
"""
  (C) Copyright 2020 Intel Corporation.

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
from __future__ import print_function

from avocado import fail_on
from apricot import TestWithServers
from daos_utils import DaosCommand
from command_utils import CommandFailure
from server_utils import ServerFailed


class DaosServerTest(TestWithServers):
    """Daos server tests.

    Test Class Description:
        Test to verify that the daos_io_server, daos_server starts/stops
        and reformat.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a DaosServerTest object."""
        super(DaosServerTest, self).__init__(*args, **kwargs)

    @fail_on(ServerFailed)
    @fail_on(CommandFailure)
    def restart_daos_server(self, reformat=True):
        """method to perform server stop and start.
        Args:
            reformat (bool): always reformat storage, could be destructive.

        """
        self.log.info("=Restart daos_server, server stop().")
        self.server_managers[0].stop()
        self.log.info("=Restart daos_server, prepare().")
        self.server_managers[0].prepare()
        self.log.info("=Restart daos_server, detect_format_ready().")
        self.server_managers[0].detect_format_ready()
        self.log.info("=Restart daos_server, dmg storage_format.")
        self.server_managers[0].dmg.storage_format(reformat)
        self.log.info("=Restart daos_server, detect_io_server_start().")
        self.server_managers[0].detect_io_server_start()

    @fail_on(ServerFailed)
    @fail_on(CommandFailure)
    def restart_daos_io_server(self, force=True):
        """method to perform io_server stop and start by dmg.
        Args:
            force (bool): Force to stop the daos io_server.
            Defaults to True.

        """
        self.server_managers[0].dmg.system_stop(force)
        self.server_managers[0].dmg.system_start()

    def get_pool_list(self):
        """method to get the pool list contents"""
        pool_list = self.get_dmg_command().get_output("pool_list")
        self.log.info("get_pool-list: %s", pool_list)
        return pool_list

    def verify_pool_list(self, expected_pool_list=None):
        """method to verify the pool list"""
        if expected_pool_list is None:
            expected_pool_list = []
        pool_list = self.get_pool_list()
        self.log.info(
            "\n===Current pool-list:  %s\n===Expected pool-list: %s\n",
            pool_list, expected_pool_list)
        self.assertEqual(
            pool_list, expected_pool_list,
            "##Current pool-list mismatch with the expected pool-list.")

    def create_pool_and_container(self):
        """method to create pool and container"""
        scm_size = self.params.get("scm_size", "/run/server/*/", 138000000)
        num_of_pool = self.params.get("num_of_pool", "/run/server/*/", 3)
        container_per_pool = self.params.get(
            "container_per_pool", "/run/server/*/", 2)
        for _ in range(num_of_pool):
            dmg = self.get_dmg_command()
            result = dmg.pool_create(scm_size)
            uuid = result['uuid']
            svc = result['svc']
            daos_cmd = DaosCommand(self.bin)
            for _ in range(container_per_pool):
                result = daos_cmd.container_create(pool=uuid, svc=svc)
                self.log.info("container create status: %s", result)

    def test_daos_server_reformat(self):
        """JIRA ID: DAOS-3596.

        Test Description: verify reformatting DAOS is same as a fresh install.
        Steps:
        (1)Verify daos server pool list after started.
        (2)Restart server without pool been created, and verify.
        (3)Create pools, containers and verify the pools.
        (4)Perform an orderly DAOS shutdown, and Use cmd line tools to
           re-format both SSDs and NVDIMMs.
        (5)Verify after DAOS server restarted, it should appear as an empty
           fresh installation.

        :avocado: tags=all,pr,hw,large,server_test,server_reformat
        """

        self.log.info("(1)Verify daos server pool list after started.")
        self.verify_pool_list()
        self.log.info("(2)Restart server without pool created and verify.")
        self.restart_daos_server()
        self.verify_pool_list()
        self.log.info("(3)Create pools, containers.")
        self.create_pool_and_container()
        self.log.info("(4)Shutdown, restart and reformat the server")
        self.restart_daos_server()
        self.log.info("(5)Verify after server restarted.")
        self.verify_pool_list()

    def test_daos_io_server_restart(self):
        """
        JIRA ID: DAOS-3593.

        Test Description: Test cmd to perform daos io_server restart.
        Steps:
        (1)Use the cmd line to perform a controlled shutdown from a
           quiescent state (i.e. clients disconnected).
        (2)Use the cmd line to perform a controlled shutdown from a
           partially quiescent state (i.e. clients attached but no
           transactions in progress).
        (3)Force shutdown and restart the daos io-server.
        (4)Verify pool list after forced shutdown and restart the
           daos io-server.
        (5)Use the cmd line to perform a controlled shutdown when the
           daos cluster is incomplete (i.e. 1 of the 2 servers is down).

        :avocado: tags=all,pr,hw,large,server_test,server_restart
        """

        self.log.info(
            "(1)Shutdown and restart the daos io-server "
            "from a quiescent state.")
        self.agent_managers[0].stop()
        self.verify_pool_list()
        self.restart_daos_io_server()
        self.agent_managers[0].start()
        self.log.info(
            "(2)Shutdown and restart the daos io-server with pools "
            "and containers created.")
        self.create_pool_and_container()
        pool_list = self.get_pool_list()
        self.restart_daos_io_server()
        self.log.info(
            "(3)Force shutdown and restart the daos io-server.")
        self.restart_daos_io_server()
        self.log.info(
            "(4)Verify pool list after forced shutdown and restart "
            "the daos io-server.")
        self.verify_pool_list(pool_list)
        hosts = self.hostlist_servers
        self.hostlist_servers = hosts[-1]
        self.log.info(
            "(5)Restart daos io server for the last server on the cluster."
            "   self.hostlist_servers= %s", self.hostlist_servers)
        self.restart_daos_io_server()
        self.verify_pool_list(pool_list)
        # Blocked by DAOS-3883 causing intermittent failures on CI
        #self.restart_daos_io_server(force=False)
        #self.verify_pool_list(pool_list)
        self.hostlist_servers = hosts
