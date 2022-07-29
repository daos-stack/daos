#!/usr/bin/python
'''
  (C) Copyright 2020-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from itertools import product

from ior_test_base import IorTestBase


class EcodCellSizeProperty(IorTestBase):
    # pylint: disable=too-many-ancestors
    # pylint: disable=too-few-public-methods
    """EC IOR class to run tests with different container cell size.

    Test Class Description: To validate Erasure code object works with
                            different EC cell sizes property.

    :avocado: recursive
    """
    def verify_cont_ec_cell_size(self, expected_size):
        """
        Verify the container EC cell size property.

        Args:
            expected_size (int): expected container cell size
        """
        daos_cmd = self.get_daos_command()
        cont_prop = daos_cmd.container_get_prop(
            pool=self.pool.uuid, cont=self.container.uuid, properties=["ec_cell_sz"])
        actual_size = cont_prop["response"][0]["value"]

        self.assertEqual(expected_size, actual_size)

    def test_ec_pool_property(self):
        """Jira ID: DAOS-7321.

        Test Description:
            Verify container cell sized is picked from container property
            and not the pool default cell size.

        Use Case:
            Create the Pool with different EC cell size
            Create the container and verify the ec_cell_sz is same for both
                pool and container.
            Create the container with different EC cell size
            Verify the ec_cell_sz is updated for the container.
            Run IOR with data verification.
            Verify the cont ec_cell_sz property after IOR.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large,ib2
        :avocado: tags=ec,ec_ior
        :avocado: tags=ec_cell_property
        """
        ior_transfer_size = self.params.get("ior_transfer_size",
                                            '/run/ior/iorflags/*')
        cont_cell_size = self.params.get("cont_cell_size",
                                         '/run/container/*')
        # Create the pool
        self.add_pool()

        # Verify pool EC cell size
        pool_prop_expected = int(self.pool.properties.value.split(":")[1])
        self.assertEqual(pool_prop_expected,
                         self.pool.get_property("ec_cell_sz"))

        # Run IOR for different Transfer size and container cell size.
        for tx_size, cont_cell in product(ior_transfer_size,
                                          cont_cell_size):
            # Initial container
            self.add_container(self.pool, create=False)

            # Use the default pool property for container and do not update
            if cont_cell != pool_prop_expected:
                self.container.properties.update("ec_cell_sz:{}"
                                                 .format(cont_cell))

            # Create the container and open handle
            self.container.create()
            self.container.open()

            # Verify container EC cell size property
            self.verify_cont_ec_cell_size(cont_cell)

            # Update IOR Command and Run
            self.update_ior_cmd_with_pool(create_cont=False)
            self.ior_cmd.transfer_size.update(tx_size)
            self.run_ior_with_pool(create_pool=False, create_cont=False)

            # Verify container EC cell size property after IOR
            self.verify_cont_ec_cell_size(cont_cell)

            # Destroy the container.
            self.container.destroy()
