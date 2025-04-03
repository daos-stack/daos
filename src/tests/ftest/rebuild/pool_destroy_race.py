"""
  (C) Copyright 2018-2024 Intel Corporation.
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from apricot import TestWithServers
from ior_utils import write_data
from test_utils_container import add_container
from test_utils_pool import add_pool


# pylint: disable=too-few-public-methods
class RbldPoolDestroyWithIO(TestWithServers):
    """Rebuild test cases featuring IOR.

    This class contains tests for pool rebuild that feature I/O going on
    during the rebuild using IOR.

    :avocado: recursive
    """

    def test_pool_destroy_with_io(self):
        """Jira ID: DAOS-3794.

        Test Description: Destroy pool when rebuild is ongoing.
                          I/O performed using IOR.

        Use Cases:
          Create pool
          Create 5 containers
          Perform io using ior with RP_3GX replication.
          Kill one of the ranks and trigger rebuild.
          Destroy Pool during rebuild.
          Re-create pool on remaining ranks.

        :avocado: tags=all,pr
        :avocado: tags=hw,medium
        :avocado: tags=pool,rebuild,ior
        :avocado: tags=RbldPoolDestroyWithIO,test_pool_destroy_with_io
        """
        containers = []

        # set params
        targets = self.server_managers[0].get_config_value("targets")
        rank = self.params.get("rank_to_kill", "/run/testparams/*")
        engines_per_host = self.params.get("engines_per_host", "/run/server_config/*")

        # create pool
        self.log_step('Creating the first pool')
        pool = add_pool(self)

        # make sure pool looks good before we start
        checks = {
            "pi_nnodes": len(self.hostlist_servers) * engines_per_host,
            "pi_ntargets": len(self.hostlist_servers) * targets * engines_per_host,
            "pi_ndisabled": 0,
        }
        self.assertTrue(
            pool.check_pool_info(**checks),
            "Invalid pool information detected before rebuild")

        self.assertTrue(
            pool.check_rebuild_status(rs_errno=0, rs_state=1, rs_obj_nr=0, rs_rec_nr=0),
            "Invalid pool rebuild info detected before rebuild")

        # perform first set of io using IOR
        for run in range(4):
            self.log_step(f'Creating a new container ({run + 1}/4)')
            containers.append(add_container(self, pool))
            self.log_step(f'Writing data to the new container with ior ({run + 1}/4)')
            write_data(self, containers[-1])

        # Kill the server and trigger rebuild
        self.log_step(f'Starting rebuild by killing rank {rank}')
        self.server_managers[0].stop_ranks([rank], force=True)

        # Wait for rebuild to start.
        self.log_step('Wait for rebuild to start')
        pool.wait_for_rebuild_to_start(interval=1)

        rebuild_state = pool.get_rebuild_state(True)
        self.log.info("%s rebuild status:%s", str(pool), rebuild_state)

        self.log_step(f'Destroy {str(pool)} while rebuild is {rebuild_state}')
        pool.destroy()

        # Disable cleanup for all containers under the destroyed pool
        for container in containers:
            container.skip_cleanup()

        # re-create the pool of full size to verify the space was reclaimed,
        # after re-starting the server on excluded rank
        self.log_step('Creating a second pool of the same size as the first')
        pool = add_pool(self)
        self.log_step('Verify the space was reclaimed')
        pool.query()

        self.log.debug("Test Passed.")
