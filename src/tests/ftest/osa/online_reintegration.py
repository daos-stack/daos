"""
  (C) Copyright 2020-2023 Intel Corporation.
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import queue
import threading
import time

from daos_racer_utils import DaosRacerCommand
from osa_utils import OSAUtils
from test_utils_pool import add_pool
from write_host_file import write_host_file


class OSAOnlineReintegration(OSAUtils):
    # pylint: disable=too-many-ancestors
    """Online Server Addition online re-integration test class.

    Test Class Description:
        This test runs the daos_server Online reintegration test cases.

    :avocado: recursive
    """

    def setUp(self):
        """Set up for test case."""
        super().setUp()
        self.dmg_command = self.get_dmg_command()
        self.ior_test_sequence = self.params.get("ior_test_sequence", '/run/ior/iorflags/*')
        self.test_oclass = self.params.get("oclass", '/run/test_obj_class/*')
        # Recreate the client hostfile without slots defined
        self.hostfile_clients = write_host_file(self.hostlist_clients, self.workdir)
        self.pool = None
        self.ds_racer_queue = queue.Queue()
        self.daos_racer = None
        self.dmg_command.exit_status_exception = True

    def daos_racer_thread(self):
        """Start the daos_racer thread."""
        self.daos_racer = DaosRacerCommand(self.bin, self.hostlist_clients[0], self.dmg_command)
        self.daos_racer.get_params(self)
        self.daos_racer.run()

    def run_online_reintegration_test(self, num_pool, racer=False, server_boot=False, oclass=None):
        """Run the Online reintegration without data.

        Args:
            num_pool (int) : total pools to create for testing purposes.
            racer (bool) : whether pool has no data or to create some data in pool.
                Defaults to False.
            server_boot (bool) : Perform system stop/start on a rank. Defaults to False.
            oclass (str) : daos object class string (eg: "RP_2G8"). Defaults to None.
        """
        if oclass is None:
            oclass = self.ior_cmd.dfs_oclass.value
        test_seq = self.ior_test_sequence[0]
        # Create a pool
        pool = {}
        exclude_servers = (len(self.hostlist_servers) * 2) - 1

        # Exclude one rank : other than rank 0.
        rank = self.random.randint(1, exclude_servers)

        # Start the daos_racer thread
        if racer is True:
            daos_racer_thread = threading.Thread(target=self.daos_racer_thread)
            daos_racer_thread.start()
            time.sleep(30)

        for val in range(0, num_pool):
            pool[val] = add_pool(self, connect=False)
            pool[val].set_property("reclaim", "disabled")

        # Exclude and reintegrate the pool, rank and targets
        for val in range(0, num_pool):
            threads = []
            self.pool = pool[val]
            # Instantiate aggregation
            if self.test_during_aggregation is True:
                for _ in range(0, 2):
                    self.run_ior_thread("Write", oclass, test_seq)
                self.delete_extra_container(self.pool)
            # The following thread runs while performing osa operations.
            threads.append(threading.Thread(target=self.run_ior_thread,
                                            kwargs={"action": "Write",
                                                    "oclass": oclass,
                                                    "test": test_seq}))

            # Launch the IOR threads
            for thrd in threads:
                self.log.info("Thread : %s", thrd)
                thrd.start()
                time.sleep(1)
            self.pool.display_pool_daos_space("Pool space: Beginning")
            pver_begin = self.pool.get_version(True)
            self.log.info("Pool Version at the beginning %s", pver_begin)
            # Get initial total free space (scm+nvme)
            initial_free_space = self.pool.get_total_free_space(refresh=True)
            if server_boot is False:
                output = self.pool.exclude(rank)
            else:
                output = self.dmg_command.system_stop(ranks=rank, force=True)
                self.pool.wait_for_rebuild_to_start()
                self.pool.wait_for_rebuild_to_end()
                self.log.info(output)
                output = self.dmg_command.system_start(ranks=rank)
                self.pool.wait_for_rebuild_to_start()

            self.print_and_assert_on_rebuild_failure(output)
            pver_exclude = self.pool.get_version(True)
            free_space_after_exclude = self.pool.get_total_free_space(refresh=True)

            self.log.info("Pool Version after exclude %s", pver_exclude)
            # Check pool version incremented after pool exclude
            # pver_exclude should be greater than
            # pver_begin + 8 targets.
            self.assertTrue(pver_exclude > (pver_begin + 8), "Pool Version Error:  After exclude")
            self.assertTrue(initial_free_space > free_space_after_exclude,
                            "Expected space after exclude is less than initial")
            output = self.pool.reintegrate(rank)
            self.print_and_assert_on_rebuild_failure(output)
            free_space_after_reintegration = self.pool.get_total_free_space(refresh=True)

            pver_reint = self.pool.get_version(True)
            self.log.info("Pool Version after reintegrate %d", pver_reint)
            # Check pool version incremented after pool reintegrate
            self.assertTrue(pver_reint > (pver_exclude + 1),
                            "Pool Version Error:  After reintegrate")
            self.assertTrue(free_space_after_reintegration > free_space_after_exclude,
                            "Expected free space after reintegration is less than exclude")
            # Wait to finish the threads
            for thrd in threads:
                thrd.join()
                if not self.out_queue.empty():
                    self.assert_on_exception()

        # Check data consistency for IOR in future
        # Presently, we are running daos_racer in parallel
        # to IOR and checking the data consistency only
        # for the daos_racer objects after exclude
        # and reintegration.
        if racer is True:
            daos_racer_thread.join()

        for val in range(0, num_pool):
            display_string = "Pool{} space at the End".format(val)
            self.pool = pool[val]
            self.pool.display_pool_daos_space(display_string)
            self.run_ior_thread("Read", oclass, test_seq)
            self.container = self.pool_cont_dict[self.pool][0]
            self.container.check()

    def test_osa_online_reintegration(self):
        """Test ID: DAOS-5075.

        Test Description: Validate Online Reintegration

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=osa,checksum,online_reintegration
        :avocado: tags=OSAOnlineReintegration,test_osa_online_reintegration
        """
        self.log.info("Online Reintegration : Basic test")
        self.run_online_reintegration_test(1)

    def test_osa_online_reintegration_server_stop(self):
        """Test ID: DAOS-5920.

        Test Description: Validate Online Reintegration with server stop

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=osa,checksum,online_reintegration,rebuild
        :avocado: tags=OSAOnlineReintegration,test_osa_online_reintegration_server_stop
        """
        self.log.info("Online Reintegration : System stop/start")
        self.run_online_reintegration_test(1, server_boot=True)

    def test_osa_online_reintegration_without_csum(self):
        """Test ID: DAOS-5075.

        Test Description: Validate Online Reintegration without checksum

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=osa,checksum,online_reintegration
        :avocado: tags=OSAOnlineReintegration,test_osa_online_reintegration_without_csum
        """
        self.log.info("Online Reintegration : No Checksum")
        self.test_with_checksum = self.params.get("test_with_checksum", "/run/checksum/*")
        self.run_online_reintegration_test(1)

    def test_osa_online_reintegration_with_aggregation(self):
        """Test ID: DAOS-6715.

        Test Description: Reintegrate rank while aggregation is happening in parallel

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=osa,checksum,online_reintegration
        :avocado: tags=OSAOnlineReintegration,test_osa_online_reintegration_with_aggregation
        """
        self.test_during_aggregation = self.params.get("test_with_aggregation",
                                                       '/run/aggregation/*')
        self.log.info("Online Reintegration : Aggregation")
        self.run_online_reintegration_test(1)

    def test_osa_online_reintegration_oclass(self):
        """Test ID: DAOS-6715.

        Test Description: Reintegrate rank with different object class

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=osa,checksum,online_reintegration
        :avocado: tags=OSAOnlineReintegration,test_osa_online_reintegration_oclass
        """
        self.log.info("Online Reintegration : Object Class")
        for oclass in self.test_oclass:
            self.run_online_reintegration_test(1, oclass=oclass)
