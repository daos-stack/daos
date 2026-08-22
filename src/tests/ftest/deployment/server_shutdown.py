"""
  (C) Copyright 2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time
import threading
from test_utils_pool import add_pool
from osa_utils import OSAUtils

class ServerShutdownTest(OSAUtils):
    # pylint: disable=too-many-ancestors
    """Test class Description: Verify server shutdown is properly handled
    and recovered.

    :avocado: recursive
    """
    def setUp(self):
        super().setUp()
        self.ior_test_sequence = self.params.get(
            "ior_test_sequence", '/run/ior/iorflags/*')
        self.daos_command = self.get_daos_command()

    def verify_server_shutdown(self, num_pool):
        """Run IOR and shutdown servers while IO is happening between nodes.

        Args:
            num_pool : Number of pools for testing.
        """
        pool = {}

        # Create the pools.
        for val in range(0, num_pool):
            pool[val] = add_pool(self, connect=False)

        # Start the IOR run and shutdown the servers.
        for val in range(0, num_pool):
            threads = []
            self.pool = pool[val]
            # The following thread runs while performing osa operations.
            threads.append(threading.Thread(target=self.run_ior_thread,
                                            kwargs={"action": "Write",
                                                    "oclass": self.ior_cmd.dfs_oclass.value,
                                                    "test": self.ior_test_sequence[0],
                                                    "fail_on_warning": False}))
            # Launch the IOR threads
            for thrd in threads:
                self.log.info("Thread : %s", thrd)
                thrd.start()
                time.sleep(5)

            # Shutdown a server "cm power off -n <hostname for rank 0>"
            self.dmg_command.system_query()
            self.log.info("CMD: Run cm power off -n <hostname for rank 0>")
            time.sleep(20)
            done = "Powered off rank 0"
            self.print_and_assert_on_rebuild_failure(done)
            for thrd in threads:
                thrd.join()
            # Start the server now. Nodes take long time to come back up.
            self.log.info("CMD: Run cm power on -n <hostname for rank 0>")
            time.sleep(300)
            # Now reintegrate the target to appropriate rank.
            output = self.dmg_command.pool_reintegrate(self.pool.uuid, rank=0)
            time.sleep(5)
            self.print_and_assert_on_rebuild_failure(output)

        # After completing the test, check for container integrity
        for val in range(0, num_pool):
            display_string = "Pool{} space at the End".format(val)
            self.pool = pool[val]
            self.pool.display_pool_daos_space(display_string)
            self.run_ior_thread("Read", oclass=self.ior_cmd.dfs_oclass.value,
                                test=self.ior_test_sequence[0])
            self.container = self.pool_cont_dict[self.pool][0]
            kwargs = {"pool": self.pool.uuid,
                      "cont": self.container.uuid}
            output = self.daos_command.container_check(**kwargs)
            self.log.info(output)

    def test_server_shutdown(self):
        """Jira ID: DAOS-11280
        Test server shutdown failure during IO operation.

        :avocado: tags=all,manual
        :avocado: tags=hw,medium
        :avocado: tags=deployment
        :avocado: tags=server_shutdown
        """
        self.verify_server_shutdown(1)
