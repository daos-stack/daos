#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

from apricot import TestWithServers
from collections import OrderedDict


class FaultDomain(TestWithServers):
    """Test class Fault domains
    :avocado: recursive
    """

    def setUp(self):
        """Set up fault domain test."""
        self.start_agents_once = False
        self.start_servers_once = False
        self.setup_start_agents = False
        self.setup_start_servers = False
        super().setUp()

    def test_pools_in_different_domains(self):
        """The aims to:
            Be able to configure daos servers using different fault domains.
            Created pools must be in different fault domains.

           Not coded:
            If a pool has the same fault domains, a warning should be expected.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
        :avocado: tags=fault_domain,fault_domain_different_domains
        """
        test_passed = True
        error_messages = list()
        fault_paths = self.params.get("fault_path", '/run/*')
        test_servers = self.params.get("test_servers", '/run/*')

        ranks_fault_map = OrderedDict()
        #
        # In the form
        #   { 0: "/fault1",
        #     1: "/fault2",
        #     2: "/fault2",
        #     3: "/fault3"
        #   }
        for counter, test_server in enumerate(test_servers):
            ranks_fault_map[counter] = fault_paths[counter]

        # Setup the servers
        # The fault path value is set given the fault_paths list
        for counter, server in enumerate(test_servers):
            self.add_server_manager()
            self.configure_manager(
                "daos_server", self.server_managers[counter],
                [server],
                self.hostfile_servers_slots)
            self.server_managers[counter].set_config_value("fault_path",
                                                           fault_paths[counter])

        # Servers are started
        for server in self.server_managers:
            server.start()

        dmg_cmd = self.get_dmg_command()

        # Create pools with small sizes and different nranks
        # We must save the return value from pool create since it is the
        # only way at the moment where we can get the ranks used
        # in each pool, should be fixed by
        # https://jira.hpdd.intel.com/browse/DAOS-5185
        pool_list = list()
        pool_list.append(dmg_cmd.pool_create(size="18G", nranks="2"))
        pool_list.append(dmg_cmd.pool_create(size="27G", nranks="3"))
        pool_list.append(dmg_cmd.pool_create(size="18G", nranks="2"))
        pool_list.append(dmg_cmd.pool_create(size="18G", nranks="2"))
        pool_list.append(dmg_cmd.pool_create(size="27G", nranks="3"))

        # Once the pools are created we check each pool
        # and get the ranks used for it.
        # For each pool:
        # Using a list, the the fault paths of the ranks are added to it
        # The list should not contain the a duplicated fault path.
        for pool in pool_list:
            ranks = pool["ranks"]
            pool_fault_path = []
            for rank in ranks.split(","):
                rank = int(rank)
                fault_path = ranks_fault_map[rank]
                pool_fault_path.append(fault_path)
            if len(pool_fault_path) != len(set(pool_fault_path)):
                test_passed = False
                error_message = f"The pool {pool} with ranks {ranks} has the" \
                                f"following fault paths: {pool_fault_path}, " \
                                f"and must be unique."
                error_messages.append(error_message)
                self.log.error(error_message)

        self.assertTrue(test_passed, error_messages)
