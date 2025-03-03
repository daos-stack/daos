"""
  (C) Copyright 2020-2023 Intel Corporation.
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import re
import threading
import time
from multiprocessing import Queue

from exception_utils import CommandFailure
from ior_utils import run_ior, thread_run_ior
from job_manager_utils import get_job_manager
from osa_utils import OSAUtils
from test_utils_pool import add_pool
from write_host_file import write_host_file


class NvmePoolExclude(OSAUtils):
    # pylint: disable=too-many-ancestors
    """
    Test Class Description: This test runs
    NVME Pool Exclude test cases.

    :avocado: recursive
    """

    def setUp(self):
        """Set up for test case."""
        super().setUp()
        self.dmg_command = self.get_dmg_command()
        self.ior_test_sequence = self.params.get("ior_test_sequence", "/run/ior/iorflags/*")
        # Recreate the client hostfile without slots defined
        self.hostfile_clients = write_host_file(self.hostlist_clients, self.workdir)
        self.pool = None
        self.cont_list = []
        self.dmg_command.exit_status_exception = True

    def run_nvme_pool_exclude(self, num_pool, oclass=None):
        """Perform the actual testing.

        It does the following jobs:
        - Create number of TestPools
        - Start the IOR threads for running on each pools.
        - On each pool do the following:
            - Perform an IOR write (using a container)
            - Exclude a daos_server
            - Perform an IOR read/verify (same container used for write)

        Args:
            num_pool (int): total pools to create for testing purposes.
            oclass (str, optional): object class (eg: RP_2G8, S1,etc). Defaults to None
        """
        # Create a pool
        pool = {}

        if oclass is None:
            oclass = self.ior_cmd.dfs_oclass.value

        # Exclude rank :  ranks other than rank 0.
        exclude_servers = len(self.hostlist_servers) * 2
        rank_list = list(range(1, exclude_servers))

        for val in range(0, num_pool):
            pool[val] = add_pool(self, connect=False)
            pool[val].set_property("reclaim", "disabled")

        job_manager = get_job_manager(self, subprocess=None, timeout=120)
        thread_queue = Queue()
        for val in range(0, num_pool):
            self.pool = pool[val]
            self.add_container(self.pool)
            self.cont_list.append(self.container)
            rf = ''.join(self.container.properties.value.split(":"))
            rf_num = int(re.search(r"rd_fac([0-9]+)", rf).group(1))

            for test in range(0, rf_num):
                ior_test_seq = self.params.get("ior_test_sequence", "/run/ior/iorflags/*")[test]
                threads = []
                kwargs = {"thread_queue": thread_queue, "job_id": test}
                ior_kwargs = {
                    "test": self,
                    "manager": job_manager,
                    "log": "ior_thread_write_pool_{}_test_{}.log".format(val, test),
                    "hosts": self.hostlist_clients,
                    "path": self.workdir,
                    "slots": None,
                    "pool": pool[val],
                    "container": self.cont_list[-1],
                    "processes": self.params.get("np", "/run/ior/client_processes/*"),
                    "ppn": self.params.get("ppn", "/run/ior/client_processes/*"),
                    "intercept": None,
                    "plugin_path": None,
                    "dfuse": None,
                    "display_space": True,
                    "fail_on_warning": False,
                    "namespace": "/run/ior/*",
                    "ior_params": {
                        "oclass": oclass,
                        "flags": self.ior_w_flags,
                        "transfer_size": ior_test_seq[2],
                        "block_size": ior_test_seq[3]
                    }
                }
                kwargs.update(ior_kwargs)
                threads.append(threading.Thread(target=thread_run_ior, kwargs=kwargs))

                # Launch the IOR threads
                for thrd in threads:
                    self.log.info("Thread : %s", thrd)
                    thrd.start()
                    time.sleep(1)

                self.pool.display_pool_daos_space("Pool space: Before Exclude")
                pver_begin = self.pool.get_version(True)

                index = self.random.randint(1, len(rank_list))
                rank = rank_list.pop(index - 1)
                tgt_exclude = self.random.randint(1, 6)
                self.log.info("Removing rank %d, target %d", rank, tgt_exclude)

                self.log.info("Pool Version at the beginning %s", pver_begin)
                output = self.pool.exclude(rank, tgt_exclude)
                self.print_and_assert_on_rebuild_failure(output)

                pver_exclude = self.pool.get_version(True)
                self.log.info("Pool Version after exclude %s", pver_exclude)
                # Check pool version incremented after pool exclude
                self.assertTrue(pver_exclude > pver_begin, "Pool Version Error:  After exclude")

                # Wait to finish the threads
                for thrd in threads:
                    thrd.join()
                errors = 0
                while not thread_queue.empty():
                    result = thread_queue.get()
                    self.log.debug("Results from thread %s (log %s)", result["job_id"],
                                   result["log"])
                    for name in ("command", "exit_status", "interrupted", "duration"):
                        self.log.debug("  %s: %s", name, getattr(result["result"], name))
                    for name in ("stdout", "stderr"):
                        self.log.debug("  %s:", name)
                        for line in getattr(result["result"], name).splitlines():
                            self.log.debug("    %s:", line)
                    if result["result"].exit_status != 0:
                        errors += 1
                if errors:
                    self.fail("Errors running {} threads".format(errors))

                # Verify the data after pool exclude
                ior_kwargs["ior_params"]["flags"] = self.ior_r_flags
                ior_kwargs["log"] = "ior_read_pool_{}_test_{}.log".format(val, test)
                try:
                    run_ior(**ior_kwargs)
                except CommandFailure as error:
                    self.fail("Error in ior read {}.".format(error))

                display_string = "Pool{} space at the End".format(val)
                self.pool.display_pool_daos_space(display_string)
                self.container.check()

    def test_nvme_pool_excluded(self):
        """Test ID: DAOS-2086.

        Test Description: This method is called from the avocado test infrastructure. This method
            invokes NVME pool exclude testing on multiple pools.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
        :avocado: tags=nvme,checksum,nvme_osa,daos_cmd
        :avocado: tags=NvmePoolExclude,test_nvme_pool_excluded
        """
        self.run_nvme_pool_exclude(1)
