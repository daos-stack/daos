"""
  (C) Copyright 2018-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from avocado.core.exceptions import TestFail
from ClusterShell.NodeSet import NodeSet
from pydaos.raw import DaosApiError, c_uuid_to_str

from apricot import TestWithServers


class PoolEvictTest(TestWithServers):
    """
    Tests DAOS client eviction from a pool that the client is using.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize an PoolEvictTest object."""
        super().__init__(*args, **kwargs)
        self.start_agents_once = False
        self.start_servers_once = False

    def connected_pool(self, hostlist, targets=None):
        """Create a pool, check the pool file in /mnt/daos, and connect.

        Args:
            hostlist (NodeSet): Hosts where pool is created.
            targets (list): List of server ranks to create the pool. Defaults to None.

        Returns:
            TestPool: Created pool.

        """
        pool = self.get_pool(create=False)

        if targets is not None:
            pool.target_list.update(targets)

        # create pool
        pool.create()

        # Check that the pool was created
        status = pool.check_files(hostlist)
        if not status:
            self.fail("Invalid pool - pool data not detected on servers")

        # Connect to the pool
        status = pool.connect()
        if not status:
            self.fail("Pool connect failed or already connected")

        # Return connected pool
        return pool

    def get_host_list(self, ranks):
        """Get the hostnames from the rank numbers.

        The ordering of the ranks and the hostnames may not align, so use the get_host()
        method to find the mapping.

        Args:
            ranks (list): List of server ranks.

        Returns:
            NodeSet: hosts running the specified ranks.

        """
        return NodeSet.fromlist([self.server_managers[0].get_host(rank=rank) for rank in ranks])

    def verify_pool_evict(self, pool):
        """Evict and verify that it succeeded. If not, fail the test.

        Args:
            pool (TestPool): Pool to evict.
        """
        try:
            pool.dmg.exit_status_exception = False
            pool.evict()
        finally:
            pool.dmg.exit_status_exception = True

        if pool.dmg.result.exit_status != 0:
            self.fail("Pool evict failed!")

    def test_pool_evict(self):
        """
        Test Steps:
        1. Create 2 pools on all server ranks.
        2. Create another pool on half of the ranks.
        3. After creating each pool, create a container and write objects.
        4. Verify that the pool file exists in /mnt/daos.
        5. Verify that the third pool's file doesn't exist on the half of the hosts that
        this pool wasn't created on.
        6. Evict the third pool on the half of the ranks.
        7. Verify that the pool file still exists in /mnt/daos for all three pools. i.e.,
        verify that the evict didn't cause the ill effect to the pool file.
        8. For all pools, call pool_query() over API. The first two pools should
        succeed. The last pool should fail because it was evicted.
        9. For all pools, write objects to the container. The first two pools should
        succeed. The last pool should fail because it was evicted.
        10. For the first two pools, check the UUID obtained from the pool_query() in
        the previous step and verify that the evict on the third pool didn't have ill
        effect.

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=vm
        :avocado: tags=pool,pool_evict
        :avocado: tags=PoolEvictTest,test_pool_evict
        """
        # Do not use self.pool. It will cause -1002 error when disconnecting.
        pools = []
        containers = []

        host_count = len(self.hostlist_servers)
        all_ranks = list(range(host_count))
        first_half_count = int(host_count / 2)
        # half_ranks_with is the half of the all ranks that we'll create the third pool
        # on.
        half_ranks_with = list(range(first_half_count))
        # half_ranks_without is the half of the all ranks that we will not create a pool.
        half_ranks_without = list(range(first_half_count, host_count))

        # all_hosts is the list of hosts that maps to the all_ranks. e.g.,
        # rank 0: wolf-1
        # rank 1: wolf-4
        # rank 2: wolf-3
        # rank 3: wolf-2
        # then all_hosts would be ["wolf-1", "wolf-4", "wolf-3", "wolf-2"]. (Order
        # doesn't matter.)
        all_hosts = self.get_host_list(ranks=all_ranks)
        # Same idea as all_hosts. i.e., ["wolf-1", "wolf-4"] in above example.
        half_hosts_with = self.get_host_list(ranks=half_ranks_with)
        half_hosts_without = self.get_host_list(ranks=half_ranks_without)

        # Step 1 to 4. Create two pools, container, and write data.
        for _ in range(2):
            # Create a pool over all of the hosts, check the pool file, and connect.
            pools.append(self.connected_pool(hostlist=all_hosts, targets=all_ranks))
            # Create a container and write data to it.
            containers.append(self.get_container(pool=pools[-1]))
            containers[-1].write_objects()

        # Create a pool over the half of the hosts, check the pool file, and connect.
        pools.append(self.connected_pool(
            hostlist=half_hosts_with, targets=half_ranks_with))
        # Create a container and write data to it.
        containers.append(self.get_container(pool=pools[-1]))
        containers[-1].write_objects()

        # Step 5. Verify that the third pool's file doesn't exist on the half of the
        # hosts that this pool wasn't created on.
        if pools[-1].check_files(half_hosts_without):
            self.fail("Pool # 2 with UUID {} exists".format(pools[-1].uuid))
        else:
            self.log.info("Pool # 2 with UUID %s does not exist", pools[-1].uuid)

        # 6. Evict the third pool.
        self.verify_pool_evict(pool=pools[-1])

        # 7. Verify that the pool file still exists in /mnt/daos for all three pools.
        for index, pool in enumerate(pools):
            # Get the hostnames to search the pool file.
            if index in (0, 1):
                hosts = all_hosts
                failure_expected = False
            else:
                hosts = half_hosts_with
                failure_expected = True

            if pool.check_files(hosts):
                self.log.info(
                    "Pool # %d with UUID %s still exists", index, pool.uuid)
            else:
                self.fail(
                    "Pool # {} with UUID {} does not exist".format(index, pool.uuid))

            # 8. Verify connection to pools with pool_query(); pool that was evicted
            # should fail because the handle was removed.
            try:
                # Call daos api directly to avoid connecting to pool.
                pool_info = pool.pool.pool_query()
                if failure_expected:
                    self.fail(
                        "Pool # {} was evicted, but pool_query worked!".format(index))
            except DaosApiError as error:
                # Expected error for evicted pool.
                if failure_expected and "-1002" in str(error):
                    self.log.info(
                        "Pool # %d was unable to query pool info due to "
                        "expected invalid handle error (-1002):\n\t%s",
                        index, error)
                # unexpected error from pool_query
                else:
                    self.fail(
                        "Pool # {} failed pool query: {}".format(index, error))

                pool_info = None

            # 9. Try to write object to the container. It should fail for the evicted pool
            # and should work for other pools.
            try:
                containers[index].write_objects()
                if failure_expected:
                    self.fail(
                        "Pool {} was evicted, but write_objects worked!".format(index))
            except TestFail as error:
                if failure_expected and "-1002" in str(error):
                    msg = "Pool # {}: write_objects failed as expected.\n\t{}".format(
                        index, error)
                    self.log.info(msg)
                else:
                    self.fail("Pool # {} write_objects failed! {}".format(index, error))

            # 10. Check that we were able to obtain the UUID of the non-evicted pools.
            if pool_info:
                pool.connected = False
                if c_uuid_to_str(pool_info.pi_uuid) == pool.uuid:
                    self.log.info(
                        "Pool # %d UUID matches pool_info.pi_uuid %s", index, pool.uuid)
                else:
                    self.fail(
                        "Pool # {} UUID does not match pool_info.pi_uuid: "
                        "{} != {}".format(
                            index, pool.uuid, c_uuid_to_str(pool_info.pi_uuid)))
