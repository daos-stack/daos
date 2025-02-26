"""
  (C) Copyright 2018-2023 Intel Corporation.
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time

from apricot import TestWithServers
from general_utils import DaosTestError, get_random_bytes
from test_utils_container import TestContainerData


class FullPoolContainerCreate(TestWithServers):
    """
    Class for test to create a container in a pool with no remaining free space.
    :avocado: recursive
    """

    def test_no_space_cont_create(self):
        """JIRA ID: DAOS-1169 DAOS-7374

        Test Description:
            Purpose of the test is to verify pool and container behave as
            expected in the completely filled scenario.

        Use Case:
            Create Pool and Container.
            Fill the pool completely with different object sizes.
            Verify return code is as expected (-1007) when no more
            data can be written to the a container.
            Once Pool is completely filled, destroy the container
            and verify container can be destroyed in filled state.
            After deleting the container and when aggregation is
            complete, verify the returned space is close enough to
            the original free space.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=container
        :avocado: tags=FullPoolContainerCreate,test_no_space_cont_create
        """
        # full storage rc
        err = "-1007"

        # test params
        threshold_percent = self.params.get("threshold_percent", "/run/pool/*")

        # create pool and connect
        self.add_pool()

        # query the pool
        self.log.info("Pool Query before write")
        self.pool.set_query_data()
        self.log.info("%s query data: %s\n", str(self.pool), self.pool.query_data)

        # create a container
        self.add_container(self.pool)
        self.container.open()

        # get free space before write
        free_space_before = self.pool.get_pool_free_space()
        self.log.info("Pool free space before write: %s", free_space_before)

        # generate random dkey, akey each time
        # write 1M until no space, then 10K, etc. to fill pool quickly
        for obj_sz in [1048576, 10240, 10, 1]:
            write_count = 0
            while True:
                self.log.debug("writing obj %s sz %s to container", write_count, obj_sz)
                my_str = b"a" * obj_sz
                dkey = get_random_bytes(5)
                akey = get_random_bytes(5)
                try:
                    self.container.written_data.append(TestContainerData(False))
                    self.container.written_data[-1].write_record(
                        self.container, akey, dkey, my_str, obj_class='OC_SX')
                    self.log.debug("wrote obj %s, sz %s", write_count, obj_sz)
                    write_count += 1
                except DaosTestError as excep:
                    if err not in repr(excep):
                        self.log.error("caught exception while writing object: %s", repr(excep))
                        self.container.close()
                        self.fail("caught exception while writing object: {}".format(repr(excep)))
                    else:
                        self.log.info("pool is too full for %s byte objects", obj_sz)
                        break

        # query the pool
        self.log.info("Pool Query after filling")
        self.pool.set_query_data()
        self.log.info("%s query data: %s\n", str(self.pool), self.pool.query_data)

        # destroy container
        self.container.destroy()

        # check for free space to be returned back once aggregation is complete
        # checking for a closer returned space value instead of exact value
        # as the test is using scm only
        counter = 1
        threshold_value = free_space_before - (free_space_before * threshold_percent)
        free_space = self.pool.get_pool_free_space()
        while free_space < threshold_value:
            # try to wait for 4 x 30 secs for aggregation to be completed or
            # else exit the test with a failure.
            if counter > 4:
                self.log.info("Free space when test terminated: %s", free_space)
                self.log.info("Threshold value when test terminated: %s", threshold_value)
                self.fail("Aggregation did not complete as expected")
            time.sleep(30)
            free_space = self.pool.get_pool_free_space()
            counter += 1
