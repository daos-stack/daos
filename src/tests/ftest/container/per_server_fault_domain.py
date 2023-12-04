"""
  (C) Copyright 2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from ior_test_base import IorTestBase


class PerServerFaultDomainTest(IorTestBase):
    """
    Test Class Description:
    The granularity of redundancy factor can be changed to node (rf_lvl:2). This means
    that in a two-engine setup, if one of the ranks goes down, we count as one. If both of
    the ranks in the same node go down, we still count as one. The default mode is engine
    (rf_lvl:1).

    If the number of ranks that has gone down exceeds the rf value, container status would
    become UNCLEAN. For example, with an rf:1 container, if two ranks go down, the status
    would be UNCLEAN. If one rank goes down, it would be HEALTHY.

    Verify the node mode by stopping one/two ranks in one/two nodes.

    Assume that the two ranks in the same node map to the same IP address.

    :avocado: recursive
    """

    def verify_per_server_fault(self, create_pool, ranks_to_stop, properties,
                                expected_status):
        """Run the main test steps.

        1. Create a pool if create_pool is True. Then create a container with given
        container property.
        2. Run IOR to write some data to the container.
        3. Stop the ranks_to_stop ranks.
        4. Wait for rebuild to finish.
        5. Get container's health status.
        6. Restart the stopped servers for cleanup.
        7. Verify container's Health status is expected.

        Args:
            create_pool (bool): Whether to create a pool.
            ranks_to_stop (str): Comma-separated ranks to stop.
            properties (str): Container property defined in the test yaml (rf_1 or rf_2).
            expected_status: Expected container status at the end. HEALTHY or UNCLEAN.
        """
        # 1. Create a pool if create_pool is True. Then create a container with given
        # container property.
        if create_pool:
            self.pool = self.get_pool()
        self.container = self.get_container(pool=self.pool, properties=properties)

        # Run IOR to write some data to the container.
        self.ior_cmd.set_daos_params(self.server_group, self.pool, self.container.identifier)
        manager = self.get_ior_job_manager_command()
        self.run_ior(manager=manager, processes=1)

        # 4. Stop the ranks.
        dmg_command = self.get_dmg_command()
        dmg_command.system_stop(ranks=ranks_to_stop)

        # 5. Wait for rebuild to finish.
        self.pool.wait_for_rebuild_to_start(interval=10)
        self.pool.wait_for_rebuild_to_end(interval=10)

        # 6. Get container's health status.
        get_prop_out = self.container.get_prop(properties=["status"])
        status = get_prop_out["response"][0]["value"]

        # 7. Restart the stopped servers for cleanup.
        dmg_command.system_start(ranks=ranks_to_stop)

        # 8. Verify container status.
        self.assertEqual(
            status, expected_status,
            "Container status isn't {} with {}!".format(expected_status, properties))

    def test_rf1_healthy(self):
        """Jira ID: DAOS-11200

        Test Description:
        1. Determine the ranks to stop; two ranks in the same node.
        2. Create a pool and a container with rf_lvl:2,rf:1
        3. Run IOR to write some data to the container.
        4. Stop the ranks.
        5. Wait for rebuild to finish.
        6. Get container's health status.
        7. Restart the stopped servers for cleanup.
        8. Verify container's Health status is HEALTHY.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=container
        :avocado: tags=PerServerFaultDomainTest,test_rf1_healthy
        """
        # 1. Determine the ranks to stop; two ranks in the same node.
        # Select one host. (It doesn't matter which host to select.)
        selected_host = list(set(self.server_managers[0].ranks.values()))[0]
        ranks_to_stop = ",".join(
            [str(rank) for rank, host in self.server_managers[0].ranks.items()
             if host == selected_host])
        self.log.info("Ranks to stop = %s", ranks_to_stop)

        properties = self.params.get("rf_1", "/run/cont_property/*")

        # Run step 2 to 8.
        self.verify_per_server_fault(
            create_pool=True, ranks_to_stop=ranks_to_stop, properties=properties,
            expected_status="HEALTHY")

    def test_rf1_unclean(self):
        """Jira ID: DAOS-11200

        Test Description:
        1. Determine the ranks to stop; two ranks in different node.
        2. Create a pool and a container with rf_lvl:2,rf:1
        3. Run IOR to write some data to the container.
        4. Stop the ranks.
        5. Wait for rebuild to finish.
        6. Get container's health status.
        7. Restart the stopped servers for cleanup.
        8. Verify container's Health status is UNCLEAN.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=container
        :avocado: tags=PerServerFaultDomainTest,test_rf1_unclean
        """
        # 1. Determine the ranks to stop; two ranks in different node.
        rank_to_host = self.server_managers[0].ranks
        rank_a = 0
        rank_b = None
        for rank in range(8):
            if rank != rank_a:
                if rank_to_host[rank] != rank_to_host[rank_a]:
                    rank_b = rank
                    break
        ranks_to_stop = f"{str(rank_a)},{str(rank_b)}"
        self.log.info("Ranks to stop = %s", ranks_to_stop)

        properties = self.params.get("rf_1", "/run/cont_property/*")

        # Run step 2 to 8.
        self.verify_per_server_fault(
            create_pool=True, ranks_to_stop=ranks_to_stop, properties=properties,
            expected_status="UNCLEAN")

    def test_rf2_healthy(self):
        """Jira ID: DAOS-11200

        Test Description:
        1. Determine the ranks to stop. Four ranks in two nodes. Select up to two service
        ranks.
        2. Create a container with rf_lvl:2,rf:2
        3. Run IOR to write some data to the container.
        4. Stop the ranks.
        5. Wait for rebuild to finish.
        6. Get container's health status.
        7. Restart the stopped servers for cleanup.
        8. Verify container's Health status is HEALTHY.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=container
        :avocado: tags=PerServerFaultDomainTest,test_rf2_healthy
        """
        # 1. Determine the ranks to stop; four ranks in two nodes. We can select up to two
        # ranks from service ranks. If we stop more than two service ranks, many of the
        # dmg commands will hang.
        # Create a pool to determine the service ranks.
        self.pool = self.get_pool()
        self.log.info("Pool service ranks = %s", self.pool.svc_ranks)

        # Create the list of non-service ranks. Assume there are 5 service ranks and rank
        # numbering is consecutive.
        non_svc_ranks = []
        for rank in range(8):
            if rank not in self.pool.svc_ranks:
                non_svc_ranks.append(rank)
        self.log.info("non_svc_ranks = %s", non_svc_ranks)

        # Prepare rank to host dictionary.
        rank_to_host = self.server_managers[0].ranks

        # Select first element in the list and find the other rank that's on the same
        # node using rank to host dictionary.
        stop_rank_1 = non_svc_ranks[0]
        stop_rank_2 = None
        for rank in range(8):
            if rank != stop_rank_1:
                if rank_to_host[rank] == rank_to_host[stop_rank_1]:
                    stop_rank_2 = rank
                    break
        self.log.info("Stop rank 1 = %s; 2 = %s", stop_rank_1, stop_rank_2)

        # Check if the rank found in the previous step is in the list. If so, remove it.
        if stop_rank_2 in non_svc_ranks:
            non_svc_ranks.remove(stop_rank_2)

        # Remove the first element (stop_rank_1) from the list.
        non_svc_ranks.remove(stop_rank_1)

        # Now the list contains one or two elements. Select the first element and find the
        # other rank that's on the same node as before.
        stop_rank_3 = non_svc_ranks[0]
        stop_rank_4 = None
        for rank in range(8):
            if rank != stop_rank_3:
                if rank_to_host[rank] == rank_to_host[stop_rank_3]:
                    stop_rank_4 = rank
                    break
        self.log.info("Stop rank 3 = %s; 4 = %s", stop_rank_3, stop_rank_4)

        ranks_to_stop = f"{stop_rank_1},{stop_rank_2},{stop_rank_3},{stop_rank_4}"
        self.log.info("Ranks to stop = %s", ranks_to_stop)

        properties = self.params.get("rf_2", "/run/cont_property/*")

        # Run step 2 to 8.
        self.verify_per_server_fault(
            create_pool=False, ranks_to_stop=ranks_to_stop, properties=properties,
            expected_status="HEALTHY")

    def test_rf2_unclean(self):
        """Jira ID: DAOS-11200

        Test Description:
        1. Determine the ranks to stop. Three ranks in three nodes. Select up to two
        service ranks.
        2. Create a container with rf_lvl:2,rf:2
        3. Run IOR to write some data to the container.
        4. Stop the ranks.
        5. Wait for rebuild to finish.
        6. Get container's health status.
        7. Restart the stopped servers for cleanup.
        8. Verify container's Health status is HEALTHY.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=container
        :avocado: tags=PerServerFaultDomainTest,test_rf2_unclean
        """
        # 1. Determine the ranks to stop; three ranks in three nodes. We can select up to
        # two ranks from service ranks. If we stop more than two service ranks, many of
        # the dmg commands will hang.
        # Create a pool to determine the service ranks.
        self.pool = self.get_pool()
        self.log.info("Pool service ranks = %s", self.pool.svc_ranks)

        # Create the list of non-service ranks. Assume there are 5 service ranks and rank
        # numbering is consecutive.
        non_svc_ranks = []
        rank_count = self.server_managers[0].engines
        self.log.info("Rank count = %s", rank_count)
        for rank in range(rank_count):
            if rank not in self.pool.svc_ranks:
                non_svc_ranks.append(rank)
        self.log.info("non_svc_ranks = %s", non_svc_ranks)

        # Prepare rank to host dictionary.
        rank_to_host = self.server_managers[0].ranks

        # Prepare host set, which contains the host of selected ranks to stop.
        stop_rank_host = set()

        # Select first element in the list (arbitrary).
        non_svc_rank = non_svc_ranks[0]
        stop_rank_host.add(rank_to_host[non_svc_rank])
        ranks_to_stop = []
        ranks_to_stop.append(non_svc_rank)

        # Iterate ranks and select the other two using rank to host dictionary and the
        # host set.
        for rank in range(8):
            if rank not in ranks_to_stop:
                host = rank_to_host[rank]
                if host not in stop_rank_host:
                    # Rank on different node found.
                    ranks_to_stop.append(rank)
                    stop_rank_host.add(host)
                    if len(ranks_to_stop) == 3:
                        break
        self.log.info("Stop rank list = %s", ranks_to_stop)

        # Convert the list to string.
        ranks_to_stop_str = ""
        for rank in ranks_to_stop:
            if ranks_to_stop_str == "":
                ranks_to_stop_str = str(rank)
            else:
                ranks_to_stop_str += "," + str(rank)
        self.log.info("Ranks to stop = %s", ranks_to_stop_str)

        properties = self.params.get("rf_2", "/run/cont_property/*")

        # Run step 2 to 8.
        self.verify_per_server_fault(
            create_pool=False, ranks_to_stop=ranks_to_stop_str, properties=properties,
            expected_status="UNCLEAN")
