#!/usr/bin/python
"""
  (C) Copyright 2020-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

from apricot import TestWithServers, skipForTicket


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

    @skipForTicket("DAOS-7919")
    def test_pools_in_different_domains(self):
        """This aims to:
            Be able to configure daos servers using different fault domains.
            Created pools must be in different fault domains.

           Not coded, should be available when DAOS-7919 is resolved.
           If a pool has the same fault domains, a debug message
             should be expected.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=fault_domain
        :avocado: tags=fault_domain_different_domains,test_pools_in_different_domains
        """
        test_passed = True
        rank = None
        error_messages = list()
        fault_paths = self.params.get("fault_path", '/run/*')
        number_pools = self.params.get("number_pools", '/run/*')

        for counter, server in enumerate(self.hostlist_servers):
            self.add_server_manager()
            self.configure_manager(
                "daos_server",
                self.server_managers[counter],
                [server],
                self.hostfile_servers_slots
            )
            self.server_managers[counter].set_config_value("fault_path",
                                                           fault_paths[counter])

        # Servers are started
        for server in self.server_managers:
            server.start()
        self.start_agents()

        # Create pools, setting the values obtained from yaml file
        self.pool = []
        for index in range(number_pools):
            namespace = "/run/pool_{}/*".format(index)
            self.log.info(namespace)
            self.pool.append(self.get_pool(namespace=namespace, connect=False))

        # Once the pools are created we check each pool
        # and get the ranks used for it.
        # For each pool:
        # Using a list, the fault paths of the ranks are added to the same list
        # The list should not contain any duplicated fault path.
        for pool in self.pool:
            pool_fault_path = []
            for rank in pool.svc_ranks:
                rank = int(rank)
                fault_path = fault_paths[rank]
                pool_fault_path.append(fault_path)
            if len(pool_fault_path) != len(set(pool_fault_path)):
                test_passed = False
                if rank is None:
                    rank = len(pool.svc_ranks)
                error_message = f"The pool {pool} with ranks {rank} has the" \
                                f"following fault paths: {pool_fault_path}, " \
                                f"and must be unique."
                error_messages.append(error_message)
                self.log.error(error_message)

        self.assertTrue(test_passed, error_messages)
