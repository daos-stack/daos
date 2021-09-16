#!/usr/bin/python3
"""
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time
import threading

from osa_utils import OSAUtils
from write_host_file import write_host_file
from dmg_utils import check_system_query_status
from apricot import skipForTicket


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
        self.dmg_command = self.get_dmg_command()
        self.daos_command = self.get_daos_command()
        self.ior_test_sequence = self.params.get("ior_test_sequence",
                                                 '/run/ior/iorflags/*')
        # Start an additional server.
        self.extra_servers = self.params.get("test_servers",
                                             "/run/extra_servers/*")
        # Recreate the client hostfile without slots defined
        self.hostfile_clients = write_host_file(
            self.hostlist_clients, self.workdir, None)
        self.pool = None
        self.dmg_command.exit_status_exception = True

    def run_nvme_pool_extend(self, num_pool, oclass=None):
        """Run Pool Extend
        Args:
            num_pool (int) : total pools to create for testing purposes.
            oclass (str) : object class (eg: RP_2G8,etc)
                           Defaults to None.
        """
        self.pool = []
        total_servers = len(self.hostlist_servers) * 2
        self.log.info("Total Daos Servers (Initial): %d", total_servers)
        if oclass is None:
            oclass = self.ior_cmd.dfs_oclass.value

        for val in range(0, num_pool):
            # Create a pool
            self.pool.append(self.get_pool())
            self.pool[-1].set_property("reclaim", "disabled")

        # On each pool (max 3), extend the ranks
        # eg: ranks : 4,5 ; 6,7; 8,9.
        for val in range(0, num_pool):
            test = self.ior_test_sequence[val]
            threads = []
            threads.append(threading.Thread(target=self.run_ior_thread,
                                            kwargs={"action": "Write",
                                                    "oclass": oclass,
                                                    "test": test,
                                                    "pool": self.pool[val]}))
            # Launch the IOR threads
            for thrd in threads:
                self.log.info("Thread : %s", thrd)
                thrd.start()
                time.sleep(1)

            self.pool[val].display_pool_daos_space("Pool space: Beginning")
            pver_begin = self.get_pool_version()

            # Start the additional servers and extend the pool
            if val == 0:
                self.log.info("Extra Servers = %s", self.extra_servers)
                self.start_additional_servers(self.extra_servers)
                # Check the system map extra servers are in joined state.
                for retry in range(0, 10):
                    scan_info = self.get_dmg_command().system_query()
                    if not check_system_query_status(scan_info):
                        if retry == 9:
                            self.fail("One/More servers status not correct")
                    else:
                        break
            self.log.info("Pool Version at the beginning %s", pver_begin)
            # Extend ranks (4,5), (6,7), (8,9)
            ranks_extended = "{},{}".format((val * 2) + 4, (val * 2) + 5)
            output = self.dmg_command.pool_extend(self.pool[val].uuid,
                                                  ranks_extended)
            self.print_and_assert_on_rebuild_failure(output)
            pver_extend = self.get_pool_version()
            self.log.info("Pool Version after extend %s", pver_extend)
            # Check pool version incremented after pool extend
            self.assertTrue(pver_extend > pver_begin,
                            "Pool Version Error:  After extend")
            # Wait to finish the threads
            for thrd in threads:
                thrd.join()
                if not self.out_queue.empty():
                    self.assert_on_exception()
            # Verify the data after pool extend
            self.run_ior_thread("Read", oclass, test)
            # Get the pool space at the end of the test
            display_string = "Pool{} space at the End".format(val)
            self.pool[val].display_pool_daos_space(display_string)
            self.container = self.pool_cont_dict[self.pool[val]][0]
            kwargs = {"pool": self.pool[val].uuid,
                      "cont": self.container.uuid}
            output = self.daos_command.container_check(**kwargs)
            self.log.info(output)

    @skipForTicket("DAOS-7195")
    def test_nvme_pool_extend(self):
        """Test ID: DAOS-2086
        Test Description: NVME Pool Extend

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
        :avocado: tags=nvme,checksum,nvme_osa
        :avocado: tags=nvme_pool_extend
        """
        self.run_nvme_pool_extend(3)
