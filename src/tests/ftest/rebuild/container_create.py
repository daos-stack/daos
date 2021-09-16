"""
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from avocado.core.exceptions import TestFail
from apricot import TestWithServers, skipForTicket
from command_utils_base import CommandFailure
from job_manager_utils import Mpirun
from ior_utils import IorCommand
from test_utils_container import TestContainer


class RbldContainerCreate(TestWithServers):
    """Rebuild with container creation test cases.

    Test Class Description:
        These rebuild tests verify the ability to create additional containers
        while rebuild is ongoing.

    :avocado: recursive
    """

    # Cancel any tests with tickets already assigned
    CANCEL_FOR_TICKET = [
        ["DAOS-2434", "rank", 1],
        ["DAOS-2434", "rank", 2],
    ]

    def add_containers_during_rebuild(self, loop_id, qty, pool1, pool2):
        """Add containers to a pool while rebuild is still in progress.

        Args:
            loop_id (str): loop identification string
            qty (int): the number of containers to create
            pool1 (TestPool): pool used to determine if rebuild is complete
            pool2 (TestPool): pool used to add containers

        """
        count = 0
        while not pool1.rebuild_complete() and count < qty:
            # Create a new container
            count += 1
            self.log.info(
                "%s: Creating container %s/%s in pool %s during rebuild",
                loop_id, count, qty, pool2.uuid)
            self.container.append(TestContainer(pool2))
            self.container[-1].get_params(self)
            self.container[-1].create()
            self.container[-1].write_objects()

        if count < qty:
            self.fail(
                "{}: Rebuild completed with only {}/{} containers "
                "created".format(loop_id, count, qty))

    def run_ior(self, loop_id, mpirun):
        """Run the ior command defined by the specified ior command object.

        Args:
            loop_id (str): loop identification string
            mpirun (Mpirun): mpirun command object to run ior
        """
        total_bytes = mpirun.job.get_aggregate_total(mpirun.processes.value)
        try:
            mpirun.run()
        except CommandFailure as error:
            self.fail(
                "{}: Error populating the container with {} bytes of data "
                "prior to target exclusion: {}".format(
                    loop_id, total_bytes, error))
        self.log.info(
            "%s: %s %s bytes to the container",
            loop_id, "Wrote" if "-w" in mpirun.job.flags.value else "Read",
            total_bytes)

    def access_container(self, loop_id, index, message):
        """Open and close the specified container.

        Args:
            loop_id (str): loop identification string
            index (int): index of the daos container object to open/close
            message (str): additional text describing the container

        Returns:
            bool: was the opening and closing of the container successful

        """
        status = True
        self.log.info(
            "%s: Verifying the container %s created during rebuild",
            loop_id, message)
        try:
            self.container[index].read_objects()
            self.container[index].close()

        except TestFail as error:
            self.log.error(
                "%s:  - Container read failed:", loop_id, exc_info=error)
            status = False

        return status

    @skipForTicket("DAOS-3550")
    def test_rebuild_container_create(self):
        """Jira ID: DAOS-1168.

        Test Description:
            Configure 4 servers and 1 client with 1 or 2 pools and a pool
            service leader quantity of 2.  Add 1 container to the first pool
            configured with 3 replicas.  Populate the container with 1GB of
            objects.  Exclude a server that has shards of this object and
            verify that rebuild is initiated.  While rebuild is active, create
            1000 additional containers in the same pool or the second pool
            (when available).  Finally verify that rebuild completes and the
            pool info indicates the correct number of rebuilt objects and
            records.  Also confirm that all 1000 additional containers created
            during rebuild are accessible.

        Use Cases:
            Basic rebuild of container objects of array values with sufficient
            numbers of rebuild targets and no available rebuild targets.

        :avocado: tags=all,full_regression
        :avocado: tags=medium
        :avocado: tags=rebuild,rebuild_cont_create
        """
        # Get test params
        targets = self.params.get("targets", "/run/server_config/*")
        pool_qty = self.params.get("pools", "/run/test/*")
        loop_qty = self.params.get("loops", "/run/test/*")
        cont_qty = self.params.get("containers", "/run/test/*")
        cont_obj_cls = self.params.get("container_obj_class", "/run/test/*")
        rank = self.params.get("rank", "/run/test/*")
        use_ior = self.params.get("use_ior", "/run/test/*", False)
        node_qty = len(self.hostlist_servers)

        # Get pool params
        self.pool = []
        for index in range(pool_qty):
            self.pool.append(self.get_pool(create=False))

        if use_ior:
            # Get ior params
            self.job_manager = Mpirun(IorCommand())
            self.job_manager.job.get_params(self)
            self.job_manager.assign_hosts(
                self.hostlist_clients, self.workdir,
                self.hostfile_clients_slots)
            self.job_manager.assign_processes(len(self.hostlist_clients))
            self.job_manager.assign_environment(
                self.job_manager.job.get_default_env("mpirun"))

        errors = [0 for _ in range(loop_qty)]
        for loop in range(loop_qty):
            # Log the start of the loop
            loop_id = "LOOP {}/{}".format(loop + 1, loop_qty)
            self.log.info("%s", "-" * 80)
            self.log.info("%s: Starting loop", loop_id)

            # Start this loop with a fresh list of containers
            self.container = []

            # Create the requested number of pools
            info_checks = []
            rebuild_checks = []
            for pool in self.pool:
                pool.create()
                info_checks.append(
                    {
                        "pi_uuid": pool.uuid,
                        "pi_ntargets": node_qty * targets,
                        "pi_nnodes": node_qty,
                        "pi_ndisabled": 0,
                    }
                )
                rebuild_checks.append(
                    {
                        "rs_errno": 0,
                        "rs_done": 1,
                        "rs_obj_nr": 0,
                        "rs_rec_nr": 0,
                    }
                )

            # Check the pool info
            status = True
            for index, pool in enumerate(self.pool):
                status &= pool.check_pool_info(**info_checks[index])
                status &= pool.check_rebuild_status(**rebuild_checks[index])
                pool.display_pool_daos_space("after creation")
            self.assertTrue(
                status,
                "Error verifying pool info prior to excluding rank {}".format(
                    rank))

            # Create a container with 1GB of data in the first pool
            if use_ior:
                self.job_manager.job.flags.update(
                    "-v -w -W -G 1 -k", "ior.flags")
                self.job_manager.job.dfs_destroy.update(
                    False, "ior.dfs_destroy")
                self.job_manager.job.set_daos_params(
                    self.server_group, self.pool[0])
                self.log.info(
                    "%s: Running IOR on pool %s to fill container %s with data",
                    loop_id, self.pool[0].uuid,
                    self.job_manager.job.dfs_cont.value)
                self.run_ior(loop_id, self.job_manager)
            else:
                self.container.append(TestContainer(self.pool[0]))
                self.container[-1].get_params(self)
                self.container[-1].create()
                self.log.info(
                    "%s: Writing to pool %s to fill container %s with data",
                    loop_id, self.pool[0].uuid, self.container[-1].uuid)
                self.container[-1].object_qty.value = 8
                self.container[-1].record_qty.value = 64
                self.container[-1].data_size.value = 1024 * 1024
                self.container[-1].write_objects(rank, cont_obj_cls)
                rank_list = self.container[-1].get_target_rank_lists(
                    " after writing data")
                self.container[-1].get_target_rank_count(rank, rank_list)

            # Display the updated pool space usage
            for pool in self.pool:
                pool.display_pool_daos_space("after container creation")

            # Exclude the first rank from the first pool to initiate rebuild
            self.server_managers[0].stop_ranks([rank], self.d_log)

            # Wait for rebuild to start
            self.pool[0].wait_for_rebuild(True, 1)

            # Create additional containers in the last pool
            start_index = len(self.container)
            self.add_containers_during_rebuild(
                loop_id, cont_qty, self.pool[0], self.pool[-1])

            # Confirm rebuild completes
            self.pool[0].wait_for_rebuild(False, 1)

            # Check the pool info
            info_checks[0]["pi_ndisabled"] += targets
            rebuild_checks[0]["rs_done"] = 1
            rebuild_checks[0]["rs_obj_nr"] = ">=0"
            rebuild_checks[0]["rs_rec_nr"] = ">=0"
            for index, pool in enumerate(self.pool):
                status &= pool.check_pool_info(**info_checks[index])
                status &= pool.check_rebuild_status(**rebuild_checks[index])
            self.assertTrue(status, "Error verifying pool info after rebuild")

            # Verify that each of created containers exist by opening them
            for index in range(start_index, len(self.container)):
                count = "{}/{}".format(
                    index - start_index + 1, len(self.container) - start_index)
                if not self.access_container(loop_id, index, count):
                    errors[loop] += 1

            # Destroy the containers created during rebuild
            for index in range(start_index, len(self.container)):
                self.container[index].destroy()

            # Read the data from the container created before rebuild
            if use_ior:
                self.log.info(
                    "%s: Running IOR on pool %s to verify container %s",
                    loop_id, self.pool[0].uuid,
                    self.job_manager.job.dfs_cont.value)
                self.job_manager.job.flags.update(
                    "-v -r -R -G 1 -E", "ior.flags")
                self.job_manager.job.dfs_destroy.update(True, "ior.dfs_destroy")
                self.run_ior(loop_id, self.job_manager)
            else:
                self.log.info(
                    "%s: Reading pool %s to verify container %s",
                    loop_id, self.pool[0].uuid, self.container[0].uuid)
                self.assertTrue(
                    self.container[0].read_objects(),
                    "Error verifying data written before rebuild")
                self.container[0].destroy()

            # Destroy the pools
            for pool in self.pool:
                pool.destroy(1)

            self.log.info(
                "%s: Loop %s", loop_id,
                "passed" if errors[loop] == 0 else "failed")

        self.log.info("Test %s", "passed" if sum(errors) == 0 else "failed")
