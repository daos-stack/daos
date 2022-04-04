#!/usr/bin/python
'''
  (C) Copyright 2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''

import re
from apricot import TestWithServers

class PoolPDAProperty(TestWithServers):
    # pylint: disable=too-many-ancestors
    # pylint: disable=too-few-public-methods
    """run tests with pool pda property

    Test Class Description: To validate pool pda works properly

    :avocado: recursive
    """
    def verify_cont_pda(self, expected_value, match_str):
        """
        Verify the container pda property.

        Args:
            expected_value (int): expected container pda value
        """
        daos_cmd = self.get_daos_command()
        cont_prop = daos_cmd.container_get_prop(self.pool.uuid,
                                                self.container.uuid)
        cont_prop_stdout = cont_prop.stdout_text
        pda_value = int(re.search(match_str + r".*([\d]+)", cont_prop_stdout).group(1))
        self.assertEqual(expected_value, pda_value)

    def test_pda_pool_property(self):
        """Jira ID: DAOS-9550.

        Test Description:
            Verify container pda is picked from container property
            and not the pool default pda.

        Use Case:
            Create the Pool with default.
            Create the pool with specified ec_pda,rp_pda
            Create the container and verify the pda is same for both
                pool and container.
            Create the container with different pda value
            Verify the pda is updated for the container.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=pool
        :avocado: tags=pool_pda_property
        """

        # Create the pool with default
        self.add_pool(namespace="/run/pool/*")

        # Verify pool ec_pda, pool_pda is default.
        self.assertEqual(1, self.pool.get_property("ec_pda"))
        self.assertEqual(3, self.pool.get_property("rp_pda"))

        # destroy pool
        self.destroy_pools(pools=self.pool)

        # create pool
        self.add_pool(namespace="/run/pool_1/*")

        # create container with default
        self.add_container(self.pool, create=True)
        ec_pda = self.pool.get_property("ec_pda")
        rp_pda = self.pool.get_property("rp_pda")

        match_ec_str = 'Performance domain affinity level of EC'
        match_rp_str = 'Performance domain affinity level of RP'
        self.verify_cont_pda(ec_pda, match_ec_str)
        self.verify_cont_pda(rp_pda, match_rp_str)
        # Destroy the container.
        self.container.destroy()

        ec_pda, rp_pda = self.params.get("pda_properties", '/run/container_1/*')
        self.add_container(self.pool, namespace="/run/container_1/*", create=False)
        self.container.properties.update("ec_pda:{},rp_pda:{}".format(ec_pda, rp_pda))

        # Create the container
        self.container.create()

        self.verify_cont_pda(ec_pda, match_ec_str)
        self.verify_cont_pda(rp_pda, match_rp_str)

        # Destroy the container.
        self.container.destroy()

        self.destroy_pools(pools=self.pool)
