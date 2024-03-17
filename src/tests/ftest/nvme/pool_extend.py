"""
  (C) Copyright 2020-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import threading
import time

from dmg_utils import check_system_query_status
from osa_utils import OSAUtils
from write_host_file import write_host_file


class NvmePoolExtend(OSAUtils):
    # pylint: disable=too-many-ancestors
    """
    Test Class Description: This test runs
    NVME Pool Extend test cases.
    - Start the few daos servers.
    - Create a pool
    - Run IOR with write mode
    - Start a new server and extend the pool
    - Verify IOR written data after extending the pool.

    :avocado: recursive
    """

    def setUp(self):
        """Set up for test case."""
        super().setUp()

        # Recreate the client hostfile without slots defined
        self.hostfile_clients = write_host_file(self.hostlist_clients, self.workdir)
        self.dmg_command.exit_status_exception = True

    def run_nvme_pool_extend(self, num_pool, oclass=None):
        """Run Pool Extend.

        Args:
            num_pool (int) : total pools to create for testing purposes.
            oclass (str) : object class (eg: RP_2G8,etc)
                           Defaults to None.
        """
        extra_servers = self.get_hosts_from_yaml(
            "test_servers", "server_partition", "server_reservation", "/run/extra_servers/*")
        ior_test_sequence = self.params.get("ior_test_sequence", '/run/ior/iorflags/*')

        total_servers = len(self.hostlist_servers) * 2
        self.log.info("Total Daos Servers (Initial): %d", total_servers)
        if oclass is None:
            oclass = self.ior_cmd.dfs_oclass.value

        # Create the pools
        pools = []
        for _ in range(0, num_pool):
            pools.append(self.get_pool(namespace="/run/pool_qty_{}/*".format(num_pool),
                         properties="reclaim:disabled"))

        # On each pool (max 3), extend the ranks
        # eg: ranks : 4,5 ; 6,7; 8,9.
        for index, pool in enumerate(pools):
            test = ior_test_sequence[index]
            threads = []
            threads.append(
                threading.Thread(
                    target=self.run_ior_thread,
                    kwargs={"action": "Write", "oclass": oclass, "test": test, "pool": pool}))

            # Launch the IOR threads
            for thread in threads:
                self.log.info("Thread : %s", thread)
                thread.start()
                time.sleep(1)

            # Get the pool version before the extend
            pool.display_pool_daos_space("Pool space: Beginning")
            version_start = pool.get_version()
            self.log.info("Pool Version at the beginning %s", version_start)

            # Start the additional servers and extend the pool
            if index == 0:
                self.log.info("Extra Servers = %s", extra_servers)
                self.start_additional_servers(extra_servers)

                # Check the system map extra servers are in joined state.
                all_joined = False
                retry = 0
                while not all_joined and retry < 10:
                    all_joined = check_system_query_status(self.get_dmg_command().system_query())
                    retry += 1
                if not all_joined:
                    self.fail("One or more extra servers failed to join: {}".format(extra_servers))

            # Extend ranks (4,5), (6,7), (8,9)
            ranks_extended = "{},{}".format((index * 2) + 4, (index * 2) + 5)
            pool.extend(ranks_extended)

            # Wait for rebuild to complete
            pool.wait_for_rebuild_to_start()
            pool.wait_for_rebuild_to_end(interval=3)
            rebuild_status = pool.get_rebuild_status()
            self.log.info("Rebuild Status: %s", rebuild_status)
            if rebuild_status in ["failed", "scanning", "aborted", "busy"]:
                self.fail("Rebuild failed after pool extend: status={}".format(rebuild_status))

            # Get the pool version after the extend
            pool.display_pool_daos_space("Pool space: After extend and rebuild")
            version_after = pool.get_version()
            self.log.info("Pool Version after extend %s", version_after)

            # Check pool version incremented after pool extend
            self.log.info("Pool Version change %s -> %s", version_start, version_after)
            if version_after <= version_start:
                self.fail(
                    "Pool version did not increment after pool extend: before={}, after={}".format(
                        version_start, version_after))

            # Wait to finish the threads
            for thread in threads:
                thread.join()
                if not self.out_queue.empty():
                    self.assert_on_exception()

            # Verify the data after pool extend
            self.run_ior_thread("Read", oclass, test)

            # Get the pool space at the end of the test
            display_string = "Pool{} space at the End".format(index)
            pool.display_pool_daos_space(display_string)
            self.container = self.pool_cont_dict[pool][0]
            self.container.check()

    def test_nvme_pool_extend(self):
        """Test ID: DAOS-2086.

        Test Description: NVME Pool Extend

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
        :avocado: tags=nvme,checksum,nvme_osa,rebuild,daos_cmd
        :avocado: tags=NvmePoolExtend,test_nvme_pool_extend
        """
        self.run_nvme_pool_extend(2)
