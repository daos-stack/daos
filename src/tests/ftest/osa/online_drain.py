"""
  (C) Copyright 2020-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import random
import threading
import time

from osa_utils import OSAUtils
from test_utils_pool import add_pool
from write_host_file import write_host_file


class OSAOnlineDrain(OSAUtils):
    # pylint: disable=too-many-ancestors
    """
    Test Class Description: This test runs
    daos_server Online Drain test cases.

    :avocado: recursive
    """
    def setUp(self):
        """Set up for test case."""
        super().setUp()
        self.dmg_command = self.get_dmg_command()
        self.ior_test_sequence = self.params.get(
            "ior_test_sequence", '/run/ior/iorflags/*')
        self.test_oclass = self.params.get("oclass", '/run/test_obj_class/*')
        # Recreate the client hostfile without slots defined
        self.hostfile_clients = write_host_file(self.hostlist_clients, self.workdir)
        self.dmg_command.exit_status_exception = True
        self.pool = None

    def run_online_drain_test(self, num_pool, oclass=None, app_name="ior"):
        """Run the Online drain without data.

        Args:
             num_pool (int) : total pools to create for testing purposes.
             oclass (str) : Object class type (RP_2G1, etc)
             app_name (str) : application to run on parallel (ior or mdtest). Defaults to ior.
        """
        # Create a pool
        pool = {}
        target_list = []
        if oclass is None:
            oclass = self.ior_cmd.dfs_oclass.value
        test_seq = self.ior_test_sequence[0]
        drain_servers = (len(self.hostlist_servers) * 2) - 1

        # Exclude target : random two targets  (target idx : 0-7)
        exc = random.randint(0, 6)  # nosec
        target_list.append(exc)
        target_list.append(exc + 1)
        t_string = "{},{}".format(target_list[0], target_list[1])

        # Drain one of the ranks (or server)
        rank = random.randint(1, drain_servers)  # nosec

        for val in range(0, num_pool):
            pool[val] = add_pool(self, connect=False)
            pool[val].set_property("reclaim", "disabled")

        # Drain the rank and targets
        for val in range(0, num_pool):
            self.pool = pool[val]
            threads = []
            # Instantiate aggregation
            if self.test_during_aggregation is True:
                for _ in range(0, 2):
                    self.run_ior_thread("Write", oclass, test_seq)
                self.delete_extra_container(self.pool)
            # The following thread runs while performing osa operations.
            if app_name == "ior":
                threads.append(threading.Thread(target=self.run_ior_thread,
                                                kwargs={"action": "Write",
                                                        "oclass": oclass,
                                                        "test": test_seq}))
            else:
                threads.append(threading.Thread(target=self.run_mdtest_thread))

            # Launch the IOR threads
            for thrd in threads:
                self.log.info("Thread : %s", thrd)
                thrd.start()
                time.sleep(1)
            # Wait the threads to write some data before drain.
            time.sleep(5)
            self.pool.display_pool_daos_space("Pool space: Beginning")
            pver_begin = self.pool.get_version(True)
            self.log.info("Pool Version at the beginning %s", pver_begin)
            # Get initial total free space (scm+nvme)
            initial_free_space = self.pool.get_total_free_space(refresh=True)
            output = self.pool.drain(rank, t_string)
            self.print_and_assert_on_rebuild_failure(output)
            free_space_after_drain = self.pool.get_total_free_space(refresh=True)

            pver_drain = self.pool.get_version(True)
            self.log.info("Pool Version after drain %s", pver_drain)
            # Check pool version incremented after pool exclude
            self.assertTrue(pver_drain > pver_begin, "Pool Version Error:  After drain")
            self.assertTrue(initial_free_space > free_space_after_drain,
                            "Expected free space after drain is less than initial")
            # Wait to finish the threads
            for thrd in threads:
                thrd.join()
                if not self.out_queue.empty():
                    self.assert_on_exception()

        if app_name == "ior":
            for val in range(0, num_pool):
                self.pool = pool[val]
                display_string = "Pool{} space at the End".format(val)
                self.pool.display_pool_daos_space(display_string)
                self.run_ior_thread("Read", oclass, test_seq)
                self.container = self.pool_cont_dict[self.pool][0]
                self.container.daos.env['UCX_LOG_LEVEL'] = 'error'
                self.container.check()

    def test_osa_online_drain(self):
        """Test ID: DAOS-4750
        Test Description: Validate Online drain with checksum
        enabled.

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=osa,checksum
        :avocado: tags=osa_drain,online_drain,online_drain_with_csum
        :avocado: tags=OSAOnlineDrain,test_osa_online_drain
        """
        self.log.info("Online Drain : With Checksum")
        self.run_online_drain_test(1)

    def test_osa_online_drain_no_csum(self):
        """Test ID: DAOS-6909
        Test Description: Validate Online drain without enabling
        checksum.

        :avocado: tags=all,pr,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=osa
        :avocado: tags=osa_drain,online_drain,online_drain_without_csum
        :avocado: tags=OSAOnlineDrain,test_osa_online_drain_no_csum
        """
        self.log.info("Online Drain : No Checksum")
        self.test_with_checksum = self.params.get("test_with_checksum",
                                                  '/run/checksum/*')
        self.run_online_drain_test(1)

    def test_osa_online_drain_oclass(self):
        """Test ID: DAOS-6909
        Test Description: Validate Online drain with different
        object class.

        :avocado: tags=all,pr,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=osa,checksum
        :avocado: tags=osa_drain,online_drain,online_drain_oclass
        :avocado: tags=OSAOnlineDrain,test_osa_online_drain_oclass
        """
        self.log.info("Online Drain : Oclass")
        for oclass in self.test_oclass:
            self.run_online_drain_test(1, oclass=oclass)

    def test_osa_online_drain_with_aggregation(self):
        """Test ID: DAOS-6909
        Test Description: Validate Online drain with different
        object class.

        :avocado: tags=all,pr,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=osa,checksum
        :avocado: tags=osa_drain,online_drain,online_drain_with_aggregation
        :avocado: tags=OSAOnlineDrain,test_osa_online_drain_with_aggregation
        """
        self.log.info("Online Drain : Aggregation")
        self.test_during_aggregation = self.params.get("test_with_aggregation",
                                                       '/run/aggregation/*')
        self.run_online_drain_test(1)

    def test_osa_online_drain_mdtest(self):
        """Test ID: DAOS-4750
        Test Description: Validate Online drain with mdtest
        running during the testing.

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=osa,checksum
        :avocado: tags=osa_drain,online_drain,online_drain_mdtest
        :avocado: tags=OSAOnlineDrain,test_osa_online_drain_mdtest
        """
        self.log.info("Online Drain : With Mdtest")
        self.run_online_drain_test(1, app_name="mdtest")
