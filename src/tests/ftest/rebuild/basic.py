"""
  (C) Copyright 2018-2024 Intel Corporation.
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from apricot import TestWithServers


class RbldBasic(TestWithServers):
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
        for _ in range(pool_quantity):
            pools.append(self.get_pool(create=False))
            containers.append(self.get_container(pools[-1], create=False))
        rank = self.params.get("rank", "/run/testparams/*")
        obj_class = self.params.get("object_class", "/run/testparams/*")

        # Collect server configuration information
        server_count = len(self.hostlist_servers)
        engine_count = self.server_managers[0].get_config_value("engines_per_host")
        engine_count = 1 if engine_count is None else int(engine_count)
        target_count = int(self.server_managers[0].get_config_value("targets"))
        self.log.info(
            "Running with %s servers, %s engines per server, and %s targets per engine",
            server_count, engine_count, target_count)

        # Create the pools and confirm their status
        status = True
        for pool in pools:
            pool.create()
            status &= pool.check_pool_info(
                pi_nnodes=server_count * engine_count,
                pi_ntargets=server_count * engine_count * target_count,
                pi_ndisabled=0
            )
            status &= pool.check_rebuild_status(rs_state=1, rs_obj_nr=0, rs_rec_nr=0, rs_errno=0)
        self.assertTrue(status, "Error confirming pool info before rebuild")

        # Create containers in each pool and fill them with data
        rs_obj_nr = []
        rs_rec_nr = []
        for container in containers:
            container.create()
            container.write_objects(rank, obj_class)

        # Determine how many objects will need to be rebuilt
        for container in containers:
            target_rank_lists = container.get_target_rank_lists(" prior to rebuild")
            rebuild_qty = container.get_target_rank_count(rank, target_rank_lists)
            rs_obj_nr.append(rebuild_qty)
            self.log.info(
                "Expecting %s/%s rebuilt objects in container %s after excluding rank %s",
                rs_obj_nr[-1], len(target_rank_lists), container, rank)
            rs_rec_nr.append(rs_obj_nr[-1] * container.record_qty.value)
            self.log.info(
                "Expecting %s/%s rebuilt records in container %s after excluding rank %s",
                rs_rec_nr[-1], container.object_qty.value * container.record_qty.value, container,
                rank)

        # Manually exclude the specified rank
        for index, pool in enumerate(pools):
            if index == 0:
                self.server_managers[0].stop_ranks([rank], True)
            else:
                # Use the direct dmg pool exclude command to avoid updating the pool version again
                pool.exclude([rank])

        # Wait for recovery to start for first pool.
        pools[0].wait_for_rebuild_to_start()

        # Wait for recovery to complete
        for pool in pools:
            pool.wait_for_rebuild_to_end()

        # Check the pool information after the rebuild
        status = True
        for index, pool in enumerate(pools):
            status &= pool.check_pool_info(
                pi_nnodes=server_count * engine_count,
                pi_ntargets=server_count * engine_count * target_count,
                pi_ndisabled=target_count
            )
            status &= pool.check_rebuild_status(
                rs_state=2, rs_errno=0)
        self.assertTrue(status, "Error confirming pool info after rebuild")

        # Verify the data after rebuild
        for container in containers:
            container.set_prop(prop="status", value="healthy")
            if container.object_qty.value != 0:
                self.assertTrue(container.read_objects(), "Data verification error after rebuild")
        self.log.info("Test Passed")

    def test_simple_rebuild(self):
        """JIRA ID: DAOS-XXXX Rebuild-001.

        Test Description:
            The most basic rebuild test.

        Use Cases:
            single pool rebuild, single client, various record/object counts

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=rebuild,pool,daos_cmd
        :avocado: tags=RbldBasic,test_simple_rebuild
        """
        self.run_rebuild_test(1)

    def test_multipool_rebuild(self):
        """JIRA ID: DAOS-XXXX (Rebuild-002).

        Test Description:
            Expand on the basic test by rebuilding 2 pools at once.

        Use Cases:
            multi-pool rebuild, single client, various object and record counts

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=rebuild,pool
        :avocado: tags=RbldBasic,test_multipool_rebuild
        """
        self.run_rebuild_test(self.params.get("pool_quantity", "/run/testparams/*"))
