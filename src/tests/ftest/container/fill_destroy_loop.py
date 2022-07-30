#!/usr/bin/python3
"""
  (C) Copyright 2019-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time
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
        cont_obj_class = self.params.get("obj_class", "/run/container/*")
        block_size = self.params.get("block_size", "/run/container/*")
        akey_size = self.params.get("akey_size", "/run/container/*")
        dkey_size = self.params.get("dkey_size", "/run/container/*")
        # select a random data size based on block_size
        data_size = random.randint(block_size*0.5, block_size*1.5) # nosec

        container = self.get_container(self.pool)
        container.open()
        written_byte = 0
        write_count = 1
        data_str = get_random_bytes(data_size)
        while True:
            dkey = get_random_bytes(dkey_size)
            akey = get_random_bytes(akey_size)
            try:
                written_byte += data_size
                self.d_log.debug("--{0}writing cont-obj {1} byte data, total {2}..".format(
                    write_count, data_size, written_byte))
                container.written_data.append(TestContainerData(False))
                container.written_data[-1].write_record(
                    container, akey, dkey, data_str, obj_class=cont_obj_class)
                self.d_log.debug("--{0}wrote cont-obj, sz {1}".format(write_count, data_size))
                write_count += 1
            except DaosTestError as excep:
                if self.DER_NOSPACE in repr(excep):
                    self.log.info("--(3)written_byte={0}, Der_no_space {1} detected, pool is "
                                  "unable for an additional {2} byte object".format(
                                      written_byte, self.DER_NOSPACE, data_size))
                    free_space = self.pool.get_pool_free_space()
                    self.log.info("--(4)free_space when pool is fulfill= {}".format(free_space))
                    break
                else:
                    self.fail("#Exception while writing object: {}".format(repr(excep)))
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
        :avocado: tags=fill_cont_pool_stress
        """

        testloop = self.params.get("testloop", "/run/pool/*")

        # create pool
        self.add_pool()

        for test_loop in range(1, testloop+1):
            # query the pool and get free space before write
            self.log.info("==>Starting test loop: {} ...".format(test_loop))
            self.pool.set_query_data()
            self.log.info("--(1)Pool Query before write:\n"
                "--Pool {0} query data: {1}\n".format(self.pool.uuid, self.pool.query_data))
            free_space = self.pool.get_pool_free_space()
            self.log.info("--(2)Pool free space before write: {}".format(free_space))

            # write container until DER_NOSPACE
            self.write_pool_until_nospace()

        self.pool.set_query_data()
        self.log.info("--(5)Pool Query before exit:\n"
            "--Pool {0} query data: {1}\n".format(self.pool.uuid, self.pool.query_data))
