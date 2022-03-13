#!/usr/bin/python3
"""
  (C) Copyright 2018-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from apricot import TestWithServers
from daos_utils import DaosCommand

class RbldBasic(TestWithServers):
    """Test class for rebuild tests.

    Test Class Description:
        This class contains tests for pool rebuild.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.daos_cmd = None

    def run_rebuild_test(self, pool_quantity):
        """Run the rebuild test for the specified number of pools.

        Args:
            pool_quantity (int): number of pools to test
        """
        # Get the test parameters
        self.pool = []
        self.container = []
        self.daos_cmd = DaosCommand(self.bin)
        for _ in range(pool_quantity):
            self.pool.append(self.get_pool(create=False))
            self.container.append(
                self.get_container(self.pool[-1], create=False))
        rank = self.params.get("rank", "/run/testparams/*")
        obj_class = self.params.get("object_class", "/run/testparams/*")

        # Collect server configuration information
        server_count = len(self.hostlist_servers)
        engine_count = self.server_managers[0].get_config_value(
            "engines_per_host")
        engine_count = 1 if engine_count is None else int(engine_count)
        target_count = int(self.server_managers[0].get_config_value("targets"))
        self.log.info(
            "Running with %s servers, %s engines per server, and %s targets "
            "per engine", server_count, engine_count, target_count)

        # Create the pools and confirm their status
        status = True
        for index in range(pool_quantity):
            self.pool[index].create()
            status &= self.pool[index].check_pool_info(
                pi_nnodes=server_count * engine_count,
                pi_ntargets=server_count * engine_count * target_count,
                pi_ndisabled=0
            )
            status &= self.pool[index].check_rebuild_status(
                rs_state=1, rs_obj_nr=0, rs_rec_nr=0, rs_errno=0)
        self.assertTrue(status, "Error confirming pool info before rebuild")

        # Create containers in each pool and fill them with data
        rs_obj_nr = []
        rs_rec_nr = []
        for index in range(pool_quantity):
            self.container[index].create()
            self.container[index].write_objects(rank, obj_class)

        # Determine how many objects will need to be rebuilt
        for index in range(pool_quantity):
            target_rank_lists = self.container[index].get_target_rank_lists(
                " prior to rebuild")
            rebuild_qty = self.container[index].get_target_rank_count(
                rank, target_rank_lists)
            rs_obj_nr.append(rebuild_qty)
            self.log.info(
                "Expecting %s/%s rebuilt objects in container %s after "
                "excluding rank %s", rs_obj_nr[-1], len(target_rank_lists),
                self.container[index], rank)
            rs_rec_nr.append(
                rs_obj_nr[-1] * self.container[index].record_qty.value)
            self.log.info(
                "Expecting %s/%s rebuilt records in container %s after "
                "excluding rank %s", rs_rec_nr[-1],
                self.container[index].object_qty.value *
                self.container[index].record_qty.value,
                self.container[index], rank)

        # Manually exclude the specified rank
        for index in range(pool_quantity):
            if index == 0:
                self.server_managers[0].stop_ranks([rank], self.d_log, True)
            else:
                self.pool[index].exclude(ranks=[rank])

        # Wait for recovery to start for first pool.
        self.pool[0].wait_for_rebuild(True)

        # Wait for recovery to complete
        for index in range(pool_quantity):
            self.pool[index].wait_for_rebuild(False)

        # Check the pool information after the rebuild
        status = True
        for index in range(pool_quantity):
            status &= self.pool[index].check_pool_info(
                pi_nnodes=server_count * engine_count,
                pi_ntargets=server_count * engine_count * target_count,
                pi_ndisabled=target_count
            )
            status &= self.pool[index].check_rebuild_status(
                rs_state=2, rs_obj_nr=rs_obj_nr[index],
                rs_rec_nr=rs_rec_nr[index], rs_errno=0)
        self.assertTrue(status, "Error confirming pool info after rebuild")

        # Verify the data after rebuild
        for index in range(pool_quantity):
            self.daos_cmd.container_set_prop(
                          pool=self.pool[index].uuid,
                          cont=self.container[index].uuid,
                          prop="status",
                          value="healthy")
            if self.container[index].object_qty.value != 0:
                self.assertTrue(
                    self.container[index].read_objects(),
                    "Data verification error after rebuild")
        self.log.info("Test Passed")

    def test_simple_rebuild(self):
        """JIRA ID: DAOS-XXXX Rebuild-001.

        Test Description:
            The most basic rebuild test.

        Use Cases:
            single pool rebuild, single client, various record/object counts

        :avocado: tags=all,daily_regression
        :avocado: tags=vm,large
        :avocado: tags=rebuild
        :avocado: tags=pool,rebuild_tests,test_simple_rebuild
        """
        self.run_rebuild_test(1)

    def test_multipool_rebuild(self):
        """JIRA ID: DAOS-XXXX (Rebuild-002).

        Test Description:
            Expand on the basic test by rebuilding 2 pools at once.

        Use Cases:
            multipool rebuild, single client, various object and record counts

        :avocado: tags=all,daily_regression
        :avocado: tags=vm,large
        :avocado: tags=rebuild
        :avocado: tags=pool,rebuild_tests,test_multipool_rebuild
        """
        self.run_rebuild_test(self.params.get("quantity", "/run/testparams/*"))
