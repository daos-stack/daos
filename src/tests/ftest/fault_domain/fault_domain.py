#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

from apricot import TestWithServers


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
        # Test pools is a list of OrderedDicts
        # Here is an example of the first element of test_pools:
        # OrderedDict([('pool_0',
        #               OrderedDict([('name', 'daos_server'),
        #                            ('size', '9G'),
        #                            ('nranks', 1),
        #                            ('control_method', 'dmg')]))])
        test_pools = self.params.get("test_pools", "/run/*")
        fault_paths = self.params.get("fault_path", '/run/*')
        test_servers = self.hostlist_servers

        for counter, server in enumerate(test_servers):
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
        for index in range(len(test_pools)):
            pool_yaml_name = "pool_{}".format(index)
            namespace = test_pools[index]
            control_method = namespace[pool_yaml_name].get("control_method")
            nranks = namespace[pool_yaml_name].get("nranks")
            size = namespace[pool_yaml_name].get("size")
            self.pool.append(self.get_pool(create=False, connect=False))
            self.pool[index].control_method.update(name="control_method",
                                                   value=control_method)
            self.pool[index].nranks.update(name="nranks", value=nranks)
            self.pool[index].size.update(name="size", value=size)
            self.pool[index].create()

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
                error_message = f"The pool {pool} with ranks {rank} has the" \
                                f"following fault paths: {pool_fault_path}, " \
                                f"and must be unique."
                error_messages.append(error_message)
                self.log.error(error_message)

        self.assertTrue(test_passed, error_messages)
