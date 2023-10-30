"""
  (C) Copyright 2022-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import random

from apricot import TestWithServers
from avocado.core.exceptions import TestFail


class BoundaryPoolContainerSpace(TestWithServers):
    """Test class for pool Boundary testing.

    Test Class Description:
        Test to create a pool and container and write random data to fill the container and delete
        container, repeat the test 100 times as boundary condition.

    :avocado: recursive
    """

    DER_NOSPACE = "-1007"

    def write_pool_until_nospace(self, test_loop):
        """write pool and container until pool is full.

        Args:
            test_loop (int): test loop for log info.
        """

        # Create a container and get pool free space before write
        container = self.get_container(self.pool)
        free_space = self.pool.get_pool_free_space()
        self.log.info("--%s.(3)Pool free space before writing data to container= %s",
                      test_loop, free_space)

        # Write random data to container until pool out of space
        base_data_size = container.data_size.value
        data_written = 0
        while True:
            new_data_size = random.randint(base_data_size * 0.5, base_data_size * 1.5)  # nosec
            container.data_size.update(new_data_size, "data_size")

            try:
                container.write_objects()
            except TestFail as excep:
                # Uncomment following for debugging
                # self.log.info("%s", repr(excep))
                if self.DER_NOSPACE in str(excep):
                    self.log.info(
                        "--%s.(4)DER_NOSPACE %s detected, pool is unable for an additional"
                        " %s byte object", test_loop, self.DER_NOSPACE, container.data_size.value)
                    break
                self.fail("Test-loop {0} exception while writing object: {1}".format(
                    test_loop, repr(excep)))
            data_written += new_data_size

        # display free space and data written
        free_space_before = self.pool.get_pool_free_space()
        self.log.info("--%s.(5) %s bytes written when pool is full.", test_loop, data_written)

        # destroy container and check for free space increase
        container.destroy()
        free_space_after = self.pool.get_pool_free_space()
        self.log.info("--%s.(6)Pool full, free_space before container delete = %s",
                      test_loop, free_space_before)
        self.log.info("--%s.(7)Pool full, free_space after  container deleted= %s",
                      test_loop, free_space_after)
        if free_space_after <= free_space_before:
            self.fail("Deleting container did not free up pool space.")

    def test_fill_destroy_cont_loop(self):
        """JIRA ID: DAOS-8465

        Test Description:
            Purpose of the test is to stress pool and container space usage
            boundary, test by looping of container object_write until pool full, check for
            any other error.

        Use Case:
            repeat following steps:
            (1)Create Pool and Container.
            (2)Fill the pool with random block data size, verify return code is as expected
               when no more data can be written to the container.
            (3)Pool free space before writing data to container
            (4)Check for DER_NOSPACE -1007.
            (5)Display bytes written when pool is full before container delete.
            (6)Display free space before container delete.
            (7)Display and verify free space after container delete.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=container,pool,fill_cont_pool_stress
        :avocado: tags=BoundaryPoolContainerSpace,test_fill_destroy_cont_loop
        """
        testloop = self.params.get("testloop", "/run/pool/*")
        # create pool and enable the aggregation
        self.add_pool()
        self.pool.set_property("reclaim", "time")

        for test_loop in range(1, testloop + 1):
            # query the pool and get free space before write
            self.log.info("==>Starting test loop: %s ...", test_loop)
            self.pool.set_query_data()
            self.log.info(
                "--%s.(1)Pool Query before write:\n"
                "--%s query data: %s\n", test_loop, str(self.pool), self.pool.query_data)
            free_space = self.pool.get_pool_free_space()
            self.log.info("--%s.(2)Pool free space before container create: %s",
                          test_loop, free_space)

            self.write_pool_until_nospace(test_loop)
