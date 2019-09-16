"""
  (C) Copyright 2019 Intel Corporation.

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
  The Government's rights to use, modify, reproduce, release, perform, display,
  or disclose this software are subject to the terms of the Apache License as
  provided in Contract No. B609815.
  Any reproduction of computer software, computer software documentation, or
  portions thereof marked with this legend must also reproduce the markings.
"""
from apricot import TestWithServers, skipForTicket
from ior_utils import IorCommand, IorFailed
from daos_api import DaosApiError
from test_utils import TestPool, TestContainer


class ContainerCreate(TestWithServers):
    """Rebuild with conatiner creation test cases.

    Test Class Description:
        These rebuild tests verify the ability to create additional containers
        while rebuild is ongoing.

    :avocado: recursive
    """

    def add_containers_during_rebuild(self, loop_id, qty, pool1, pool2):
        """Add containers to a pool while rebuild is still in progress.

        Args:
            loop_id (str): loop identification string
            qty (int): the number of containers to create
            pool1 (TestPool): pool used to determine if rebuild is complete
            pool2 (TestPool): pool used to add containers

        Returns:
            list: a list of TestContianer objects

        """
        containers = []
        while not pool1.rebuild_complete() and len(containers) < qty:
            # Create a new container
            self.log.info(
                "%s: Creating container %s/%s in pool %s during rebuild",
                loop_id, len(containers) + 1, qty, pool2.uuid)
            containers.append(TestContainer(pool2))
            containers[-1].get_params(self)
            containers[-1].create()
            containers[-1].write_objects()

        if len(containers) < qty:
            self.fail(
                "{}: Rebuild completed with only {}/{} containers "
                "created".format(loop_id, len(containers), qty))

        return containers

    def run_ior(self, loop_id, ior_cmd):
        """Run the ior command defined by the specified ior command object.

        Args:
            loop_id (str): loop identification string
            ior_cmd (IorCommand): ior command object to run
        """
        processes = len(self.hostlist_clients)
        total_bytes = ior_cmd.get_aggregate_total(processes)
        try:
            ior_cmd.run(
                self.orterun, self.tmp, processes, self.hostfile_clients)
        except IorFailed as error:
            self.fail(
                "{}: Error populating the container with {} bytes of data "
                "prior to target exclusion: {}".format(
                    loop_id, total_bytes, error))
        self.log.info(
            "%s: %s %s bytes to the container",
            loop_id, "Wrote" if "-w" in ior_cmd.flags.value else "Read",
            total_bytes)

    def access_container(self, loop_id, container, message):
        """Open and close the specified container.

        Args:
            loop_id (str): loop identification string
            container (TestContainer): daos container object to open/close
            message (str): additional text describing the container

        Returns:
            bool: was the opening and closing of the container successful

        """
        status = True
        self.log.info(
            "%s: Verifying the container %s created during rebuild",
            loop_id, message)
        try:
            container.read_objects()

        except DaosApiError as error:
            self.log.error("%s:  - Container read failed: %s", loop_id, error)
            status = False

        return status

    @skipForTicket("DAOS-3076")
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

        :avocado: tags=all,medium,full_regression,rebuild,rebuildcontcreate
        """
        # Get test params
        targets = self.params.get("targets", "/run/server_config/*")
        pool_qty = self.params.get("pools", "/run/test/*")
        loop_qty = self.params.get("loops", "/run/test/*")
        cont_qty = self.params.get("containers", "/run/test/*")
        rank = self.params.get("rank", "/run/test/*")
        node_qty = len(self.hostlist_servers)

        # Get pool params
        self.pool = []
        for index in range(pool_qty):
            self.pool.append(TestPool(self.context, self.log))
            self.pool[-1].get_params(self)

        # Get ior params
        ior_cmd = IorCommand()
        ior_cmd.get_params(self)

        # Cancel any tests with tickets already assigned
        if rank == 1 or rank == 2:
            self.cancelForTicket("DAOS-2434")

        errors = [0 for _ in range(loop_qty)]
        for loop in range(loop_qty):
            # Log the start of the loop
            loop_id = "LOOP {}/{}".format(loop + 1, loop_qty)
            self.log.info("%s", "-" * 80)
            self.log.info("%s: Starting loop", loop_id)

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
            self.assertTrue(
                status,
                "Error verifying pool info prior to excluding rank {}".format(
                    rank))

            # Create a container with 1GB of data in the first pool
            ior_cmd.flags.update("-v -w -W -G 1 -k", "ior.flags")
            ior_cmd.daos_destroy.update(False, "ior.daos_destroy")
            ior_cmd.set_daos_params(self.server_group, self.pool[0])
            self.log.info(
                "%s: Running IOR on pool %s to fill container %s with data",
                loop_id, self.pool[0].uuid, ior_cmd.daos_cont.value)
            self.run_ior(loop_id, ior_cmd)

            # Exclude the first rank from the first pool to initiate rebuild
            self.pool[0].start_rebuild(self.server_group, rank, self.d_log)

            # Wait for rebuild to start
            self.pool[0].wait_for_rebuild(True, 1)

            # Create additional containers in the last pool
            new_containers = self.add_containers_during_rebuild(
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

            # Verify that each of created containers exist by openning them
            for index, container in enumerate(new_containers):
                count = "{}/{}".format(index + 1, len(new_containers))
                if not self.access_container(loop_id, container, count):
                    errors[loop] += 1

            # Destroy the containers created during rebuild
            for index, container in enumerate(new_containers):
                container.destroy()

            # Read the data from the container created before rebuild
            self.log.info(
                "%s: Running IOR on pool %s to verify container %s",
                loop_id, self.pool[0].uuid, ior_cmd.daos_cont.value)
            ior_cmd.flags.update("-v -r -R -G 1 -E", "ior.flags")
            ior_cmd.daos_destroy.update(True, "ior.daos_destroy")
            self.run_ior(loop_id, ior_cmd)

            # Destroy the pools
            for pool in self.pool:
                pool.destroy(1)

            self.log.info(
                "%s: Loop %s", loop_id,
                "passed" if errors[loop] == 0 else "failed")

        self.log.info("Test %s", "passed" if sum(errors) == 0 else "failed")
