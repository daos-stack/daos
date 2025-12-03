'''
  (C) Copyright 2022-2024 Intel Corporation.
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
import re

from exception_utils import CommandFailure
from ior_test_base import IorTestBase


class PoolRedunFacProperty(IorTestBase):
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

    def create_verify_destroy_cont(self, pool, cont_rfs, pool_prop_expected):
        """Create container, verify its rd_fac, and destroy it.

        Args:
            pool (TestPool): Pool to create container.
            cont_rfs (str): rd_fac value of container we want to set.
            pool_prop_expected (str): rd_fac value set in the pool.
        """
        for cont_rf in cont_rfs:
            # Create container
            self.add_container(pool, create=False)

            # Use the default pool property for container and do not update
            if cont_rf != pool_prop_expected:
                self.container.properties.update("rd_fac:{}".format(cont_rf))

            # Create the container and open handle
            self.container.create()

            # Verify container redundancy factor property
            self.verify_cont_rf(cont_rf)

            # Destroy the container.
            self.container.destroy()

    def verify_container_rd_fac(self, container, expected_rd_fac, msg):
        """Verify container rd_fac.

        Args:
            container (TestContainer): Container to verify.
            expected_rd_fac (int): Expected rd_fac.
            msg (str): Message to print during failure.
        """
        cont_prop_json = container.get_prop(properties=["rd_fac"])
        cont_prop_value = cont_prop_json["response"][0]["value"]
        # Container's rd_fac value has "rd_fac" in front of it. e.g., rd_fac1. The last
        # number is the value we want.
        cont_prop = re.findall(r"\d+", cont_prop_value)
        cont_prop_int = int(cont_prop[0])
        self.assertEqual(cont_prop_int, expected_rd_fac, msg)

    def test_rf_pool_property(self):
        """Test rd_fac configuration for pool and container.

        Pool's rd_fac is used to define the default rd_fac value of its container. If a
        container is created without rd_fac during container create, pool's rd_fac will be
        used. If user wants to set different rd_fac from pool's, they can pass it in as
        --properties=rd_fac:N during container create.

        Test Description:
        1. Create a pool with rd_fac passed in.
        2. Verify pool's rd_fac by calling dmg pool get-prop.
        3. Verify pool's rd_fac can't be changed to invalid value.
        4. Verify that pool and container's rd_fac haven't been changed.
        5. Verify container's default and requested rd_fac.
        6. Update pool's rd_fac to a valid value and verify.
        7. Verify container's default and requested rd_fac with updated pool rd_fac.
        8. Verify that while pool's rd_fac was changed, container's rd_fac remained the
        same.

        Jira ID: DAOS-9217, DAOS-17768

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
        :avocado: tags=pool,redundancy,redundancy_factor,rf
        :avocado: tags=PoolRedunFacProperty,test_rf_pool_property
        """
        cont_rfs = self.params.get("cont_rf", '/run/container/*')

        # 1. Create a pool with rd_fac passed in.
        self.log_step("Create a pool with rd_fac passed in.")
        pool = self.get_pool()

        # 2. Verify pool's rd_fac by calling dmg pool get-prop.
        self.log_step("Verify pool's rd_fac by calling dmg pool get-prop.")
        pool_prop_expected = int(pool.properties.value.split(":")[1])
        msg = "Unexpected initial pool rd_fac!"
        self.assertEqual(pool_prop_expected, pool.get_property("rd_fac"), msg)

        # Create a container for later steps.
        container = self.get_container(pool=pool)

        # 3. Verify pool's rd_fac can't be changed to invalid value.
        self.log_step("Verify pool's rd_fac can't be changed to invalid value.")
        invalid_rd_fac = 99
        dmg_command = self.get_dmg_command()
        try:
            properties = f"rd_fac:{invalid_rd_fac}"
            dmg_command.pool_set_prop(pool=pool.identifier, properties=properties)
            self.fail(f"Pool rd_fac was changed to invalid value: {invalid_rd_fac}!")
        except CommandFailure as command_failure:
            self.log.info(
                "Update pool's rd_fac to %s failed as expected", invalid_rd_fac)
            exp_msg = f"invalid redun fac value {invalid_rd_fac}"
            if exp_msg not in str(command_failure):
                msg = (f"Updating pool's rd_fac to invalid value didn't return expected "
                       f"message! {exp_msg}")
                self.fail(msg)

        # 4. Verify that pool and container's rd_fac haven't been changed.
        self.log_step("Verify that pool and container's rd_fac haven't been changed.")
        pool_prop = pool.get_property(prop_name="rd_fac")
        msg = "Unexpected pool rd_fac after failed to update! "
        self.assertEqual(pool_prop, pool_prop_expected, msg)
        msg = "Unexpected container rd_fac after failed to update pool rd_fac! "
        self.verify_container_rd_fac(
            container=container, expected_rd_fac=pool_prop_expected, msg=msg)

        # 5. Verify container's default and requested rd_fac.
        self.log_step("Verify container's default and requested rd_fac.")
        self.create_verify_destroy_cont(
            pool=pool, cont_rfs=cont_rfs, pool_prop_expected=pool_prop_expected)

        # 6. Update pool's rd_fac to a valid value and verify.
        self.log_step("Update pool's rd_fac to a valid value and verify.")
        new_pool_rd_fac = 0
        pool.set_property(prop_name="rd_fac", prop_value=str(new_pool_rd_fac))
        msg = "Pool rd_fac wasn't updated as expected!"
        self.assertEqual(new_pool_rd_fac, pool.get_property("rd_fac"), msg)

        # 7. Verify container's default and requested rd_fac with updated pool rd_fac.
        msg = "Verify container's default and requested rd_fac with updated pool rd_fac."
        self.log_step(msg)
        self.create_verify_destroy_cont(
            pool=pool, cont_rfs=cont_rfs, pool_prop_expected=new_pool_rd_fac)

        # 8. Verify that while pool's rd_fac was changed, container's rd_fac remained the
        # same.
        msg = ("Verify that while pool's rd_fac was changed, container's rd_fac remained "
               "the same.")
        self.log_step(msg)
        msg = "Unexpected container rd_fac after pool rd_fac was changed!"
        self.verify_container_rd_fac(
            container=container, expected_rd_fac=pool_prop_expected, msg=msg)
