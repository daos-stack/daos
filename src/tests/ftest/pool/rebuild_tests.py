#!/usr/bin/python
"""
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from apricot import TestWithServers, skipForTicket
from test_utils_pool import TestPool
from test_utils_container import TestContainer


class RebuildTests(TestWithServers):
    """Test class for rebuild tests.

    Test Class Description:
        This class contains tests for pool rebuild.

    :avocado: recursive
    """

    def run_rebuild_test(self, pool_quantity):
        """Run the rebuild test for the specified number of pools.

        Args:
            pool_quantity (int): number of pools to test
        """
        # Get the test parameters
        pools = []
        containers = []
        for index in range(pool_quantity):
            pools.append(TestPool(self.context, self.get_dmg_command()))
            pools[index].get_params(self)
            containers.append(TestContainer(pools[index]))
            containers[index].get_params(self)
        targets = self.params.get("targets", "/run/server_config/*")
        rank = self.params.get("rank", "/run/testparams/*")
        obj_class = self.params.get("object_class", "/run/testparams/*")

        # Create the pools and confirm their status
        server_count = len(self.hostlist_servers)
        status = True
        for index in range(pool_quantity):
            pools[index].create()
            status &= pools[index].check_pool_info(
                pi_nnodes=server_count,
                pi_ntargets=(server_count * targets),  # DAOS-2799
                pi_ndisabled=0
            )
            status &= pools[index].check_rebuild_status(
                rs_done=1, rs_obj_nr=0, rs_rec_nr=0, rs_errno=0)
        self.assertTrue(status, "Error confirming pool info before rebuild")

        # Create containers in each pool and fill it with data
        rs_obj_nr = []
        rs_rec_nr = []
        for index in range(pool_quantity):
            containers[index].create()
            containers[index].write_objects(rank, obj_class)

        # Determine how many objects will need to be rebuilt
        for index in range(pool_quantity):
            target_rank_lists = containers[index].get_target_rank_lists(
                " prior to rebuild")
            rebuild_qty = containers[index].get_target_rank_count(
                rank, target_rank_lists)
            rs_obj_nr.append(rebuild_qty)
            self.log.info(
                "Expecting %s/%s rebuilt objects in container %s after "
                "excluding rank %s", rs_obj_nr[-1], len(target_rank_lists),
                containers[index], rank)
            rs_rec_nr.append(
                rs_obj_nr[-1] * containers[index].record_qty.value)
            self.log.info(
                "Expecting %s/%s rebuilt records in container %s after "
                "excluding rank %s", rs_rec_nr[-1],
                containers[index].object_qty.value *
                containers[index].record_qty.value,
                containers[index], rank)

        # Manually exclude the specified rank
        for index in range(pool_quantity):
            if index == 0:
                pools[index].start_rebuild([rank], self.d_log)
            else:
                pools[index].exclude([rank], self.d_log)

        # Wait for recovery to start
        for index in range(pool_quantity):
            pools[index].wait_for_rebuild(True)

        # Wait for recovery to complete
        for index in range(pool_quantity):
            pools[index].wait_for_rebuild(False)

        # Check the pool information after the rebuild
        status = True
        for index in range(pool_quantity):
            status &= pools[index].check_pool_info(
                pi_nnodes=server_count,
                pi_ntargets=(server_count * targets),  # DAOS-2799
                pi_ndisabled=targets                   # DAOS-2799
            )
            status &= pools[index].check_rebuild_status(
                rs_done=1, rs_obj_nr=rs_obj_nr[index],
                rs_rec_nr=rs_rec_nr[index], rs_errno=0)
        self.assertTrue(status, "Error confirming pool info after rebuild")

        # Verify the data after rebuild
        for index in range(pool_quantity):
            self.assertTrue(
                containers[index].read_objects(),
                "Data verification error after rebuild")
        self.log.info("Test Passed")

    @skipForTicket("DAOS-6359")
    def test_simple_rebuild(self):
        """JIRA ID: DAOS-XXXX Rebuild-001.

        Test Description:
            The most basic rebuild test.

        Use Cases:
            single pool rebuild, single client, various record/object counts

        :avocado: tags=all,daily_regression,medium,pool,rebuild,rebuildsimple
        """
        self.run_rebuild_test(1)

    @skipForTicket("DAOS-6359")
    def test_multipool_rebuild(self):
        """JIRA ID: DAOS-XXXX (Rebuild-002).

        Test Description:
            Expand on the basic test by rebuilding 2 pools at once.

        Use Cases:
            multipool rebuild, single client, various object and record counts

        :avocado: tags=all,daily_regression,medium,pool,rebuild,rebuildmulti
        """
        self.run_rebuild_test(self.params.get("quantity", "/run/testparams/*"))
