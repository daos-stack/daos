"""
  (C) Copyright 2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import random

from apricot import TestWithServers
from general_utils import get_random_bytes, DaosTestError
from test_utils_container import TestContainerData


class BoundaryPoolContainerSpace(TestWithServers):
    """Test class for pool Boundary testing.

    Test Class Description:
        Test to create a pool and container and write random data to fill the container and delete
        container, repeat the test 100 times as boundary condition.

    :avocado: recursive
    """

    DER_NOSPACE = "-1007"

    def write_pool_until_nospace(self):
        """write pool and container until pool is full. """
        data_size_base = self.params.get("data_size_base", "/run/container/*")
        container = self.get_container(self.pool)
        container.open()
        written_byte = 0
        write_count = 1
        while True:
            data_size = random.randint(data_size_base * 0.5, data_size_base * 1.5) # nosec
            written_byte += data_size
            try:
                container.written_data.append(TestContainerData(False))
                container.written_data[-1].write_record(
                    container,
                    get_random_bytes(container.akey_size.value),
                    get_random_bytes(container.dkey_size.value),
                    get_random_bytes(data_size))
                self.log.info("--%s wrote container-obj, sz %s", write_count, data_size)
                write_count += 1
            except DaosTestError as error:
                if self.DER_NOSPACE in repr(error):
                    self.log.info(
                        "--(3)written_byte= %s, Der_no_space %s detected, pool is "
                        "unable for an additional %s byte object",
                        written_byte, self.DER_NOSPACE, data_size)
                    free_space = self.pool.get_pool_free_space()
                    self.log.info("--(4)free_space when pool is full= %s", free_space)
                    break
                self.fail("#Exception while writing container-obj: {}".format(repr(error)))
        container.destroy()

    def test_fill_destroy_cont_loop(self):
        """JIRA ID: DAOS-8465

        Test Description:
            Purpose of the test is to stress pool and container space usage
            boundary, test by looping of fulfill container and destroy

        Use Case:
            repeat following steps:
            (1)Create Pool and Container.
            (2)Fill the pool with random block data size, verify return code is as expected
               DER_NOSPACE -1007 when no more data can be written to the container.
            (3)destroy the container

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=container,pool
        :avocado: tags=fill_cont_pool_stress,test_fill_destroy_cont_loop
        """

        testloop = self.params.get("testloop", "/run/pool/*")

        # create pool
        self.add_pool()
        for test_loop in range(1, testloop + 1):
            # query the pool and get free space before write
            self.log.info("==>Starting test loop: %s ...", test_loop)
            self.pool.set_query_data()
            self.log.info(
                "--(1)Pool Query before write:\n"
                "--Pool %s query data: %s\n", self.pool.uuid, self.pool.query_data)
            free_space = self.pool.get_pool_free_space()
            self.log.info("--(2)Pool free space before write: %s", free_space)

            # write container until DER_NOSPACE
            self.write_pool_until_nospace()

        self.pool.set_query_data()
        self.log.info(
            "--(5)Pool Query before exit:\n"
            "--Pool %s query data: %s\n", self.pool.uuid, self.pool.query_data)
