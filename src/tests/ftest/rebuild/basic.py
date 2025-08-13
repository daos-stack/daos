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

    def test_rebuild_basic(self):
        """
        Test Description:
            The most basic rebuild test.

        Use Cases:
            Multiple pool rebuild, single client, various record/object counts

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=rebuild,pool,daos_cmd
        :avocado: tags=RbldBasic,test_rebuild_basic
        """
        # Get the test parameters
        rank = self.random.choice(list(self.server_managers[0].ranks.keys()))
        datasets = self.params.get("datasets", "/run/testparams/*")
        num_pools = self.params.get("num_pools", "/run/testparams/*")

        # Collect server configuration information
        server_count = len(self.hostlist_servers)
        engine_count = self.server_managers[0].get_config_value("engines_per_host")
        engine_count = 1 if engine_count is None else int(engine_count)
        target_count = int(self.server_managers[0].get_config_value("targets"))
        self.log.info(
            "Running with %s servers, %s engines per server, and %s targets per engine",
            server_count, engine_count, target_count)

        self.log_step(f"Create {num_pools} pools")
        pools = []
        status = True
        for _ in range(num_pools):
            pool = self.get_pool()
            pools.append(pool)
            status &= pool.check_pool_info(
                pi_nnodes=server_count * engine_count,
                pi_ntargets=server_count * engine_count * target_count,
                pi_ndisabled=0)
            status &= pool.check_rebuild_status(rs_state=1, rs_obj_nr=0, rs_rec_nr=0, rs_errno=0)
        self.assertTrue(status, "Error confirming pool info before rebuild")

        self.log_step(f"Create {len(datasets)} containers and fill with data")
        containers = []
        for idx, dataset in enumerate(datasets):
            # Round-robin create containers across pools
            container = self.get_container(pools[idx % len(pools)])
            containers.append(container)
            (oclass, data_size, object_qty, record_qty) = dataset
            container.update_params(
                data_size=data_size, object_qty=object_qty, record_qty=record_qty)
            container.write_objects(rank, oclass)

        # Determine how many objects will need to be rebuilt
        rs_obj_nr = []
        rs_rec_nr = []
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

        self.log_step(f"Exclude random rank {rank}")
        self.server_managers[0].stop_ranks([rank], True)
        for pool in pools:
            pool.exclude([rank])

        self.log_step("Wait for rebuild to start")
        pools[0].wait_for_rebuild_to_start(interval=3)

        self.log_step("Wait for rebuild to end")
        for pool in pools:
            pool.wait_for_rebuild_to_end(interval=3)

        self.log_step("Verify pool info after rebuild")
        status = True
        for pool in pools:
            status &= pool.check_pool_info(
                pi_nnodes=server_count * engine_count,
                pi_ntargets=server_count * engine_count * target_count,
                pi_ndisabled=target_count
            )
            status &= pool.check_rebuild_status(
                rs_state=2, rs_errno=0)
        self.assertTrue(status, "Error confirming pool info after rebuild")

        self.log_step("Verify container data after rebuild")
        for container in containers:
            container.set_prop(prop="status", value="healthy")
            if container.object_qty.value != 0:
                self.assertTrue(container.read_objects(), "Data verification error after rebuild")

        self.log_step("Test Passed")
