#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time
import threading

from test_utils_pool import TestPool
from write_host_file import write_host_file
from daos_racer_utils import DaosRacerCommand
from dmg_utils import check_system_query_status
from osa_utils import OSAUtils
from apricot import skipForTicket
from daos_utils import DaosCommand


class OSAOnlineExtend(OSAUtils):
    # pylint: disable=too-many-ancestors
    """
    Test Class Description: This test runs
    daos_server Online Extend test cases.

    :avocado: recursive
    """
    def setUp(self):
        """Set up for test case."""
        super().setUp()
        self.dmg_command = self.get_dmg_command()
        self.daos_command = DaosCommand(self.bin)
        self.ior_test_sequence = self.params.get(
            "ior_test_sequence", '/run/ior/iorflags/*')
        self.test_oclass = self.params.get("oclass", '/run/test_obj_class/*')
        self.ranks = self.params.get("rank_list", '/run/test_ranks/*')
        # Start an additional server.
        self.extra_servers = self.params.get("test_servers",
                                             "/run/extra_servers/*")
        # Recreate the client hostfile without slots defined
        self.hostfile_clients = write_host_file(
            self.hostlist_clients, self.workdir, None)
        self.pool = None
        self.dmg_command.exit_status_exception = True
        self.daos_racer = None

    def daos_racer_thread(self):
        """Start the daos_racer thread."""
        self.daos_racer = DaosRacerCommand(self.bin, self.hostlist_clients[0],
                                           self.dmg_command)
        self.daos_racer.get_params(self)
        self.daos_racer.set_environment(
            self.daos_racer.get_environment(self.server_managers[0]))
        self.daos_racer.run()

    def run_online_extend_test(self, num_pool, racer=False,
                               oclass=None, app_name="ior"):
        """Run the Online extend without data.
            Args:
             num_pool(int) : total pools to create for testing purposes.
             racer(bool) : Run the testing along with daos_racer.
                           Defaults to False.
             oclass(str) : Object Class (eg: RP_2G1, etc). Default to None.
             app_name(str) : App (ior or mdtest) to run during the testing.
                             Defaults to ior.
        """
        # Pool dictionary
        pool = {}

        if oclass is None:
            oclass = self.ior_cmd.dfs_oclass.value
        test_seq = self.ior_test_sequence[0]

        # Start the daos_racer thread
        if racer is True:
            daos_racer_thread = threading.Thread(target=self.daos_racer_thread)
            daos_racer_thread.start()
            time.sleep(30)

        for val in range(0, num_pool):
            pool[val] = TestPool(
                context=self.context, dmg_command=self.get_dmg_command(),
                label_generator=self.label_generator)
            pool[val].get_params(self)
            pool[val].create()
            pool[val].set_property("reclaim", "disabled")

        # Extend the pool_uuid, rank and targets
        for val in range(0, num_pool):
            threads = []
            self.pool = pool[val]

            # Start the additional servers and extend the pool
            self.log.info("Extra Servers = %s", self.extra_servers)
            self.start_additional_servers(self.extra_servers)
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
            # Make sure system map has all ranks in joined state.
            for retry in range(0, 10):
                scan_info = self.get_dmg_command().system_query()
                if not check_system_query_status(scan_info):
                    if retry == 9:
                        self.fail("One or more servers not in expected status")
                else:
                    break

            # Launch the IOR or mdtest thread
            for thrd in threads:
                self.log.info("Thread : %s", thrd)
                thrd.start()
                time.sleep(1)

            self.pool.display_pool_daos_space("Pool space: Beginning")
            pver_begin = self.get_pool_version()
            self.log.info("Pool Version at the beginning %s", pver_begin)
            output = self.dmg_command.pool_extend(self.pool.uuid, self.ranks)
            self.print_and_assert_on_rebuild_failure(output)

            pver_extend = self.get_pool_version()
            self.log.info("Pool Version after extend %s", pver_extend)
            # Check pool version incremented after pool exclude
            self.assertTrue(pver_extend > pver_begin,
                            "Pool Version Error:  After extend")
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
            kwargs = {"pool": self.pool.uuid,
                      "cont": self.container.uuid}
            output = self.daos_command.container_check(**kwargs)
            self.log.info(output)

    @skipForTicket("DAOS-7195,DAOS-7955")
    def test_osa_online_extend(self):
        """Test ID: DAOS-4751
        Test Description: Validate Online extend with checksum
        enabled.

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,medium,ib2
        :avocado: tags=osa,checksum
        :avocado: tags=osa_extend,online_extend,online_extend_with_csum
        """
        self.log.info("Online Extend : With Checksum")
        self.run_online_extend_test(1)

    @skipForTicket("DAOS-7195,DAOS-7955")
    def test_osa_online_extend_without_checksum(self):
        """Test ID: DAOS-6645
        Test Description: Validate Online extend without checksum enabled.

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,medium,ib2
        :avocado: tags=osa,checksum
        :avocado: tags=osa_extend,online_extend,online_extend_without_csum
        """
        self.log.info("Online Extend : Without Checksum")
        self.test_with_checksum = self.params.get("test_with_checksum",
                                                  '/run/checksum/*')
        self.run_online_extend_test(1)

    @skipForTicket("DAOS-7195,DAOS-7955")
    def test_osa_online_extend_oclass(self):
        """Test ID: DAOS-6645
        Test Description: Validate Online extend with different
        object class.

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,medium,ib2
        :avocado: tags=osa,checksum
        :avocado: tags=osa_extend,online_extend,online_extend_oclass
        """
        self.log.info("Online Extend : Oclass")
        self.run_online_extend_test(1, oclass=self.test_oclass[0])

    @skipForTicket("DAOS-7195,DAOS-7955")
    def test_osa_online_extend_mdtest(self):
        """Test ID: DAOS-6645
        Test Description: Validate Online extend with mdtest application.

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,medium,ib2
        :avocado: tags=osa,checksum
        :avocado: tags=osa_extend,online_extend,online_extend_mdtest
        """
        self.log.info("Online Extend : Mdtest")
        self.run_online_extend_test(1, app_name="mdtest")

    @skipForTicket("DAOS-7195,DAOS-7955")
    def test_osa_online_extend_with_aggregation(self):
        """Test ID: DAOS-6645
        Test Description: Validate Online extend with aggregation on.

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,medium,ib2
        :avocado: tags=osa,checksum
        :avocado: tags=osa_extend,online_extend,online_extend_with_aggregation
        """
        self.log.info("Online Extend : Aggregation")
        self.test_during_aggregation = self.params.get("test_with_aggregation",
                                                       '/run/aggregation/*')
        self.run_online_extend_test(1)
