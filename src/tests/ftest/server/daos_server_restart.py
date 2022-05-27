#!/usr/bin/python
"""
  (C) Copyright 2020-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from avocado import fail_on
from apricot import TestWithServers
from exception_utils import CommandFailure
from server_utils import ServerFailed


class DaosServerTest(TestWithServers):
    """Daos server tests.

    Test Class Description:
        Test to verify that daos_server starts/stops and reformat.

    :avocado: recursive
    """
    @fail_on(ServerFailed)
    @fail_on(CommandFailure)
    def restart_daos_server(self, force=True):
        """Perform server stop and start.

        Args:
            force (bool): always reformat storage, could be destructive.
        """
        self.log.info("=Restart daos_server, server stop().")
        self.server_managers[0].stop()
        self.log.info("=Restart daos_server, prepare().")
        self.server_managers[0].prepare()
        self.log.info("=Restart daos_server, detect_format_ready().")
        self.server_managers[0].detect_format_ready()
        self.log.info("=Restart daos_server, dmg storage_format.")
        self.server_managers[0].dmg.storage_format(force)
        for pool in self.pool:
            self.unregister_cleanup(**pool.get_cleanup_entry(self))
        self.log.info("=Restart daos_server, detect_engine_start().")
        self.server_managers[0].detect_engine_start()
        self.log.info("=Restart daos_agent, stop")
        self.stop_agents()
        self.log.info("=Restart daos_agent, start")
        self.start_agent_managers()

    @fail_on(ServerFailed)
    @fail_on(CommandFailure)
    def restart_engine(self, force=True):
        """Perform engine stop and start by dmg.

        Args:
            force (bool): Force to stop the daos engine.
            Defaults to True.
        """
        self.server_managers[0].dmg.system_stop(force)
        self.server_managers[0].dmg.system_start()

    def verify_pool_list(self, expected_pool_list=None):
        """Verify the pool list."""
        if expected_pool_list is None:
            expected_pool_list = []
        pool_list = self.get_dmg_command().get_pool_list_uuids()
        self.log.info(
            "\n===Current pool-list:  %s\n===Expected pool-list: %s\n",
            pool_list, expected_pool_list)
        self.assertEqual(
            pool_list, expected_pool_list,
            "##Current pool-list mismatch with the expected pool-list.")

    def create_pool_and_container(self):
        """Create pool and container."""
        num_of_pool = self.params.get("num_of_pool", "/run/server/*/", 3)
        container_per_pool = self.params.get(
            "container_per_pool", "/run/server/*/", 2)
        for _ in range(num_of_pool):
            self.pool.append(self.get_pool(connect=False))
            for _ in range(container_per_pool):
                self.container.append(self.get_container(self.pool[-1]))

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

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,large
        :avocado: tags=server_test,server_reformat,DAOS_5610
        """
        self.pool = []
        self.container = []

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

        self.container = None

    def test_engine_restart(self):
        """JIRA ID: DAOS-3593.

        Test Description: Test cmd to perform daos engine restart.
        Steps:
        (1)Use the cmd line to perform a controlled shutdown from a
           quiescent state (i.e. clients disconnected).
        (2)Use the cmd line to perform a controlled shutdown from a
           partially quiescent state (i.e. clients attached but no
           transactions in progress).
        (3)Force shutdown and restart the daos engine.
        (4)Verify pool list after forced shutdown and restart the
           daos engine.
        (5)Use the cmd line to perform a controlled shutdown when the
           daos cluster is incomplete (i.e. 1 of the 2 servers is down).

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,large
        :avocado: tags=server_test,server_restart,DAOS_5610
        """
        self.pool = []
        self.container = []

        self.log.info(
            "(1)Shutdown and restart the daos engine "
            "from a quiescent state.")
        self.agent_managers[0].stop()
        self.verify_pool_list()
        self.restart_engine()
        self.agent_managers[0].start()
        self.log.info(
            "(2)Shutdown and restart the daos engine with pools "
            "and containers created.")
        self.create_pool_and_container()
        pool_list = self.get_dmg_command().get_pool_list_uuids()
        self.restart_engine()
        self.log.info(
            "(3)Force shutdown and restart the daos engine.")
        self.restart_engine()
        self.log.info(
            "(4)Verify pool list after forced shutdown and restart "
            "the daos engine.")
        self.verify_pool_list(pool_list)
        hosts = self.hostlist_servers
        self.hostlist_servers = hosts[-1]
        self.log.info(
            "(5)Restart daos io server for the last server on the cluster."
            "   self.hostlist_servers= %s", self.hostlist_servers)
        self.restart_engine()
        self.verify_pool_list(pool_list)
        # Blocked by DAOS-3883 causing intermittent failures on CI
        # self.restart_engine(force=False)
        # self.verify_pool_list(pool_list)
        self.hostlist_servers = hosts
