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

import uuid

from apricot import TestWithServers
from general_utils import get_pool, get_container, kill_server
from general_utils import wait_for_rebuild, is_pool_rebuild_complete
from ior_utils import IorCommand, IorFailed
from daos_api import DaosApiError


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
            pool1 (DaosPool): pool used to determine if rebuild is complete
            pool2 (DaosPool): pool used to add containers

        Returns:
            list: a list of DaosContianer objects

        """
        containers = []
        index = 0

        while not is_pool_rebuild_complete(pool1, self.log) and index < qty:
            # Create a new container
            self.log.info(
                "%s: Creating container %s/%s in pool %s during rebuild",
                loop_id, index + 1, qty, pool2.get_uuid_str())
            containers.append(
                    get_container(self.context, pool2, self.d_log, False))

            # Create the next container
            index += 1

        if index < qty:
            self.fail(
                "{}: Rebuild completed with only {}/{} containers "
                "created".format(loop_id, index, qty))

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
            ior_cmd.run(self.basepath, processes, self.hostfile_clients)
        except IorFailed as error:
            self.fail(
                "{}: Error populating the container with {} bytes of data "
                "prior to target exclusion: {}".format(
                    loop_id, total_bytes, error))
        self.log.info(
            "%s: %s %s bytes to the container",
            loop_id, "Wrote" if "-w" in ior_cmd.flags.value else "Read",
            total_bytes)

    def access_container(self, loop_id, container, message=""):
        """Open and close the specified container.

        Args:
            loop_id (str): loop identification string
            container (DaosContainer): daos container object to open/close
            message (str): additional text describing the container

        Returns:
            bool: was the opening and closing of the container successful

        """
        status = True
        uuid_str = container.get_uuid_str()
        self.log.info(
            "%s: Opening container %s %s", loop_id, uuid_str, message)
        try:
            container.open()
            self.log.info(
                "%s: Closing container %s %s", loop_id, uuid_str, message)
            try:
                container.close()

            except DaosApiError as error:
                self.log.error("%s:  - Close failed: %s", loop_id, error)
                status = False

        except DaosApiError as error:
            self.log.error("%s:  - Open failed: %s", loop_id, error)
            status = False

        return status

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
        pool_mode = self.params.get("mode", "/run/pool/*")
        pool_size = self.params.get("size", "/run/pool/*")
        pool_name = self.params.get("setname", "/run/pool/*")
        pool_svcn = self.params.get("svcn", "/run/pool/*")
        pool_qty = self.params.get("qty", "/run/pool/*")
        container_qty = self.params.get("container_qty", "/run/container/*")
        rank = self.params.get("rank", "/run/test/*")
        loop_qty = self.params.get("loops", "/run/test/*")

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
            self.pool = []
            for index in range(pool_qty):
                self.log.info(
                    "%s: Creating pool %s/%s", loop_id, index + 1, pool_qty)
                self.pool.append(
                    get_pool(
                        self.context, pool_mode, pool_size, pool_name,
                        pool_svcn, self.d_log, False))

            # Create a container with 1GB of data in the first pool
            cont_uuid = uuid.uuid1()
            self.log.info(
                "%s: Running IOR on pool %s to create container %s",
                loop_id, self.pool[0].get_uuid_str(), cont_uuid)
            ior_cmd = IorCommand()
            ior_cmd.set_params(self)
            ior_cmd.flags.value = "-v -w -W -G 1 -k"
            ior_cmd.daos_pool.value = self.pool[0].get_uuid_str()
            ior_cmd.daos_cont.value = cont_uuid
            ior_cmd.daos_destroy.value = False
            ior_cmd.daos_svcl.value = ":".join(
                [str(item) for item in [
                    int(self.pool[0].svc.rl_ranks[index])
                    for index in range(pool_svcn)]])
            self.run_ior(loop_id, ior_cmd)

            # Connect to the pools
            for index, pool in enumerate(self.pool):
                self.log.info(
                    "%s: Connecting to pool %s (%s/%s)",
                    loop_id, pool.get_uuid_str(), index + 1, len(self.pool))
                pool.connect(1 << 1)

            # Exclude the first rank from the first pool to initiate rebuild
            self.log.info(
                "%s: Excluding DAOS server %s (rank %s)",
                loop_id, self.server_group, rank)
            kill_server(
                self.server_group, self.context, rank, self.pool[0],
                self.d_log)

            # Wait for rebuild to start
            wait_for_rebuild(self.pool[0], self.log, True, 1)

            # Create additional containers in the last pool
            new_containers = self.add_containers_during_rebuild(
                loop_id, container_qty, self.pool[0], self.pool[-1])

            # Confirm rebuild completes
            wait_for_rebuild(self.pool, self.log, False, 1)

            # Verify that each of created containers exist by openning them
            self.log.info(
                "%s: Verifying the containers created during rebuild", loop_id)
            for index, container in enumerate(new_containers):
                count = "{}/{}".format(index + 1, len(new_containers))
                if not self.access_container(loop_id, container, count):
                    errors[loop] += 1

            # Destroy the containers created during rebuild
            for index, container in enumerate(new_containers):
                self.log.info(
                    "%s: Destroying container %s (%s/%s)",
                    loop_id, container.get_uuid_str(), index + 1,
                    len(new_containers))
                container.destroy()

            # Disconnect from the pool(s) created during rebuild
            for index, pool in enumerate(self.pool):
                self.log.info(
                    "%s: Disconnecting from pool %s (%s/%s)",
                    loop_id, pool.get_uuid_str(), index + 1, len(self.pool))
                pool.disconnect()

            # Read the data from the container created before rebuild
            self.log.info(
                "%s: Running IOR on pool %s to verify container %s",
                loop_id, self.pool[0].get_uuid_str(), cont_uuid)
            ior_cmd.flags.value = "-v -r -R -G 1 -k"
            ior_cmd.daos_destroy.value = True
            self.run_ior(loop_id, ior_cmd)

            # Destroy the pools
            for index, pool in enumerate(self.pool):
                self.log.info(
                    "%s: Destroying pool %s (%s/%s)",
                    loop_id, pool.get_uuid_str(), index + 1, len(self.pool))
                pool.destroy(1)

            self.log.info(
                "%s: Loop %s", loop_id,
                "passed" if errors[loop] == 0 else "failed")

        self.log.info("Test %s", "passed" if sum(errors) == 0 else "failed")
