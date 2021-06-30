#!/usr/bin/python
"""
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import uuid
from apricot import TestWithServers, skipForTicket
from pydaos.raw import DaosApiError, c_uuid_to_str
from command_utils_base import CommandFailure
from test_utils_container import TestContainer
from general_utils import create_string_buffer


class EvictTests(TestWithServers):
    """

    Tests DAOS client eviction from a pool that the client is using.

    :avocado: recursive
    """

    def connected_pool(self, hostlist, targets=None):
        # pylint: disable=unused-argument
        """Create and connect to a pool.

        Args:
            hostlist (list): list of daos server nodes
            targets (list): List of targets for pool create

        """
        self.pool.append(self.get_pool(create=False))

        if targets is not None:
            self.pool[-1].target_list.update(targets)

        # create pool
        self.pool[-1].create()

        # Commented out due to DAOS-3836. Remove the pylint disable at the top
        # of this method when the following lines are uncommented.
        # Check that the pool was created
        # status = pool.check_files(hostlist)
        # if not status:
        #    self.fail("Invalid pool - pool data not detected on servers")

        # Connect to the pool
        status = self.pool[-1].connect()
        if not status:
            self.fail("Pool connect failed or already connected")

    def pool_handle_exist(self, test_param):
        """Check if pool handle still exists.

        Args:
            test_param (str): invalid UUID

        Returns:
            True or False, depending if the handle exists or not

        """
        status = True
        if int(self.pool[0].pool.handle.value) == 0:
            self.log.error(
                "Pool handle was removed when doing an evict with %s",
                test_param)
            status &= False
        return status

    def evict_bad_param(self, test_param):
        """Connect to pool, connect and try to evict with a bad param.

        Args:
            test_param (str): invalid UUID

        Returns:
            TestPool (bool)

        """
        # setup pool and connect
        self.pool = []
        self.connected_pool(self.hostlist_servers)

        self.log.info(
            "Pool UUID: %s\n Pool handle: %s\n",
            self.pool[0].uuid, self.pool[0].pool.handle.value,)

        original_uuid = self.pool[0].uuid

        if test_param == "invalid_uuid":
            # Attempt to evict pool with invalid UUID
            bogus_uuid = self.pool[0].uuid
            # in case uuid4() generates pool.uuid
            while bogus_uuid == self.pool[0].uuid:
                bogus_uuid = str(uuid.uuid4())

            # Set the bogus UUID to the pool.
            self.pool[0].uuid = bogus_uuid

            self.log.info(
                "Evicting pool with Invalid Pool UUID: %s", self.pool[0].uuid)
        else:
            self.fail("Invalid yaml parameters - check \"params\" values")

        # Make it not fail at CommandFailure and call.
        self.pool[0].dmg.exit_status_exception = False
        self.pool[0].evict()

        # Restore the original UUID.
        self.pool[0].uuid = original_uuid

        result = self.pool[0].dmg.result
        if result.exit_status != 0:
            self.log.info("Expected exception - invalid param %s\n %s\n",
                          test_param, str(result))

            # verify that pool still exists and the handle is still valid.
            self.log.info("Check if pool handle still exist")
            return self.pool_handle_exist(test_param)

        # if here then pool-evict did not raise an exception as expected
        # restore the valid server group name and check if valid pool
        # still exists
        self.log.info(
            "Command exception did not occur"
            " - evict from pool with %s", test_param)

        # check if pool handle still exists
        self.pool_handle_exist(test_param)

        # Commented out due to DAOS-3836.
        # if self.pool.check_files(self.hostlist_servers):
        #    self.log.error("Valid pool files were not detected on server after"
        #                   " a pool evict with %s failed to raise an "
        #                   "exception", test_param)
        self.log.error("Test did not raise an exception with when "
                       "evicting a pool with bad param: %s", test_param)
        return False

    @skipForTicket("DAOS-7377")
    def test_evict(self):
        """
        Test evicting a client from a pool.

        Test creates 2 pools on 4 target (pool_tgt [0,1,2,3])
        and 1 pool on 2 targets (pool_tgt_ut [0,1]).  The pools are connected
        to and a container with data is created on all 3.
        The evict is done on connection to the pool with 2 targets.
        The handle is removed.
        The test verifies that the other two pools were not affected
        by the evict
        :avocado: tags=all,pr,daily_regression,full_regression
        :avocado: tags=small
        :avocado: tags=pool,pool_evict
        :avocado: tags=DAOS_5610
        """
        self.pool = []
        container = []
        # non_pool_servers = []

        # Target list is configured so that the pools are across all servers
        # except the pool under test is created on half of the servers
        pool_tgt = list(range(len(self.hostlist_servers)))
        pool_tgt_ut = list(range(int(len(self.hostlist_servers)/2)))
        tlist = [pool_tgt, pool_tgt, pool_tgt_ut]
        pool_servers = [self.hostlist_servers[:len(tgt)] for tgt in tlist]
        # non_pool_servers = [self.hostlist_servers[len(tgt):] for tgt in tlist]

        # Create Connected TestPool
        for count, target_list in enumerate(tlist):
            self.connected_pool(pool_servers[count], target_list)
            # Commented out due to DAOS-3836.
            # if len(non_pool_servers[count]) > 0:
            #    self.assertFalse(
            #        pool[count].check_files(non_pool_servers[count]),
            #        "Pool # {} data detected on non pool servers {} ".format(
            #            count+1, non_pool_servers[count]))

            self.log.info(
                "Pool # %s is connected with handle %s", count + 1,
                self.pool[-1].pool.handle.value)

            container.append(TestContainer(self.pool[-1]))
            container[count].get_params(self)
            container[count].create()
            container[count].write_objects(target_list[-1])

        try:
            self.log.info(
                "Attempting to evict clients from pool with UUID: %s",
                self.pool[-1].uuid)
            # Evict the last pool in the list
            self.pool[-1].dmg.pool_evict(pool=self.pool[-1].pool.get_uuid_str())
        except CommandFailure as result:
            self.fail(
                "Detected exception while evicting a client {}".format(
                    str(result)))

        for count in range(len(tlist)):
            # Commented out due to DAOS-3836.
            # # Check that all pool files still exist
            # if pool[count].check_files(pool_servers[count]):
            #    self.log.info(
            #        "Pool # %s with UUID %s still exists",
            #        count+1, pool[count].uuid)
            # else:
            #    self.fail(
            #        "Pool # {} with UUID {} does not exists".format(
            #            count+1, pool[count].uuid))

            # Verify connection to pools with pool_query; pool that was evicted
            # should fail the pool query because the handle was removed
            try:
                # Call daos api directly to avoid connecting to pool
                pool_info = self.pool[count].pool.pool_query()
            except DaosApiError as error:
                # expected error for evicted pool
                if count == len(tlist) - 1 and "-1002" in str(error):
                    self.log.info(
                        "Pool # %s was unable to query pool info due to "
                        "expected invalid handle error (-1002):\n\t%s",
                        count+1, error)
                # unexpected error from pool_query
                else:
                    self.fail(
                        "Pool # {} failed pool query: {}".format(
                            count+1, error))

                pool_info = None
            # Check that UUID of valid pools still exists
            if pool_info:
                if c_uuid_to_str(pool_info.pi_uuid) == self.pool[count].uuid:
                    self.log.info(
                        "Pool # %s UUID matches pool_info.pi_uuid %s",
                        count+1, self.pool[count].uuid)
                else:
                    self.fail(
                        "Pool # {} UUID does not matches pool_info.pi_uuid:  "
                        "{} != {}".format(
                            count+1, self.pool[count].uuid, c_uuid_to_str(
                                pool_info.pi_uuid)))

    def test_evict_bad_uuid(self):
        """
        Test evicting a pool using an invalid uuid.

        :avocado: tags=all,pool,pr,daily_regression,full_regression,small
        :avocado: tags=poolevict
        :avocado: tags=pool_evict_bad_uuid,DAOS_5610
        """
        test_param = self.params.get("uuid", '/run/badparams/*')
        self.assertTrue(self.evict_bad_param(test_param))
