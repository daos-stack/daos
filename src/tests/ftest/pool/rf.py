'''
  (C) Copyright 2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from ior_test_base import IorTestBase


class PoolRedunFacProperty(IorTestBase):
    # pylint: disable=too-few-public-methods
    """Run tests with different pool redundancy factor.

    Test Class Description: To validate pool rf works properly

    :avocado: recursive
    """

    def verify_cont_rf(self, expected_value):
        """
        Verify the container rf property.

        Args:
            expected_value (int): expected container rf value
        """
        cont_props = self.container.get_prop(properties=["rd_fac"])
        rf_str = cont_props["response"][0]["value"]
        rf_value = int(rf_str.replace("rd_fac", ""))
        self.assertEqual(expected_value, rf_value)

    def test_rf_pool_property(self):
        """Jira ID: DAOS-9217.

        Test Description:
            Verify container rf is picked from container property
            and not the pool default rf.

        Use Case:
            Create the Pool with different rf value
            Create the container and verify the rf is same for both
                pool and container.
            Create the container with different rf value
            Verify the rf is updated for the container.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
        :avocado: tags=pool,redundancy,redundancy_factor,rf
        :avocado: tags=PoolRedunFacProperty,test_rf_pool_property
        """
        cont_rfs = self.params.get("cont_rf", '/run/container/*')

        # Create the pool
        self.add_pool()

        # Verify pool rf.
        pool_prop_expected = int(self.pool.properties.value.split(":")[1])
        self.assertEqual(pool_prop_expected, self.pool.get_property("rd_fac"))

        for cont_rf in cont_rfs:
            # Initial container
            self.add_container(self.pool, create=False)

            # Use the default pool property for container and do not update
            if cont_rf != pool_prop_expected:
                self.container.properties.update("rd_fac:{}".format(cont_rf))

            # Create the container and open handle
            self.container.create()

            # Verify container redundancy factor property
            self.verify_cont_rf(cont_rf)

            # Destroy the container.
            self.container.destroy()
