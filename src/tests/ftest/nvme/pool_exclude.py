"""
  (C) Copyright 2020-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time
import random
import threading
import re
import queue

from avocado.utils.process import CmdResult
from ior_utils import run_ior
from test_utils_pool import add_pool
from osa_utils import OSAUtils
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
        self.daos_command = self.get_daos_command()
        self.ior_test_sequence = self.params.get("ior_test_sequence",
                                                 '/run/ior/iorflags/*')
        # Recreate the client hostfile without slots defined
        self.hostfile_clients = write_host_file(
            self.hostlist_clients, self.workdir, None)
        self.pool = None
        self.cont_list = []
        self.dmg_command.exit_status_exception = True

    def run_ior_with_thread_q(thread_queue, test, manager, log, hosts, path, slots, group, pool,
                              container, processes, ppn, intercept, plugin_path, dfuse,
                              display_space, fail_on_warning, namespace, ior_params):
        """Start an IOR thread with thread queue for failure analysis.

        Args:
            test (Test): avocado Test object
            manager (JobManager): command to manage the multi-host execution of ior
            log (str): log file.
            hosts (NodeSet): hosts on which to run the ior command
            path (str): hostfile path.
            slots (int): hostfile number of slots per host.
            group (str): DAOS server group name
            pool (TestPool): DAOS test pool object
            container (TestContainer): DAOS test container object.
            processes (int): number of processes to run
            ppn (int, optional): number of processes per node to run.  If specified it will override
                the processes input. Defaults to None.
            intercept (str, optional): path to interception library. Defaults to None.
            plugin_path (str, optional): HDF5 vol connector library path. This will enable dfuse
                working directory which is needed to run vol connector for DAOS. Default is None.
            dfuse (Dfuse, optional): DAOS test dfuse object required when specifying a plugin_path.
                Defaults to None.
            display_space (bool, optional): Whether to display the pool space. Defaults to True.
            fail_on_warning (bool, optional): Controls whether the test should fail if a 'WARNING'
                is found. Default is False.
            namespace (str, optional): path to yaml parameters. Defaults to "/run/ior/*".
            ior_params (dict, optional): dictionary of IorCommand attributes to override from
                get_params(). Defaults to None.

        Raises:
            CommandFailure: if there is an error running the ior command

        Returns:
            CmdResult: result of the ior command

        """
        try:
            result = run_ior(test, manager, log, hosts, path, slots, group, pool, container,
                             processes, ppn, intercept, plugin_path, dfuse, display_space,
                             fail_on_warning, namespace, ior_params)
            thread_queue.put(result)
        except Exception as error:
            thread_queue.put(CmdResult(command="", stdout=str(error), exit_status=1))

    def test_run_ior_with_thread_q(self, thread_queue, pool, oclass, test, flags, single_cont_read=True,
                              fail_on_warning=True):
        """Start an IOR thread with thread queue for failure analysis.

        Args:
            thread_queue (str): ior test thread queue
            pool (object): pool handle
            oclass (str): IOR object class, container class.
            test (list): IOR test sequence
            flags (str): IOR flags
            single_cont_read (bool, optional): Always read from the 1st container. Defaults to True.
            fail_on_warning (bool, optional): Test terminates for IOR warnings. Defaults to True.
        """
        try:
            result = self.ior_thread(pool, oclass, test, flags, single_cont_read, fail_on_warning)
            thread_queue.put(result)
        except Exception as error:
            thread_queue.put(CmdResult(command="", stdout=str(error), exit_status=1))

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

        for val in range(0, num_pool):
            self.pool = pool[val]
            self.add_container(self.pool)
            self.cont_list.append(self.container)
            rf = ''.join(self.container.properties.value.split(":"))
            rf_num = int(re.search(r"rd_fac([0-9]+)", rf).group(1))
            for test in range(0, rf_num):
                ior_test_seq = self.params.get("ior_test_sequence", '/run/ior/iorflags/*')[test]
                threads = []
                job_manager = self.get_ior_job_manager_command()
                job_manager.timeout = timeout
                threads.append(threading.Thread(target=run_ior_with_thread_q,
                                                kwargs={"thread_queue": "",
                                                        "pool": self.pool,
                                                        "flags": self.ior_w_flags,
                                                        "oclass": oclass,
                                                        "single_cont_read": True,
                                                        "fail_on_warning": True,
                                                        "test": ior_test_seq}))
                # Launch the IOR threads
                for thrd in threads:
                    self.log.info("Thread : %s", thrd)
                    thrd.start()
                    time.sleep(1)

                self.pool.display_pool_daos_space("Pool space: Before Exclude")
                pver_begin = self.pool.get_version(True)

                index = random.randint(1, len(rank_list))  # nosec
                rank = rank_list.pop(index - 1)
                tgt_exclude = random.randint(1, 6)  # nosec
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
#                    if not self.out_queue.empty():
#                        self.assert_on_exception(self.out_queue)
                errors = 0
                while not thread_queue.empty():
                    result = thread_queue.get()
                    if result.exit_status != 0:
                        errors += 1
                    print(result)
                if errors:
                     self.fail("Errors running {} threads".format(errors))
                     # if not self.out_queue.empty():
                     #     self.assert_on_exception()

                # Verify the data after pool exclude
                self.run_ior_thread("Read", oclass, ior_test_seq)
                display_string = "Pool{} space at the End".format(val)
                self.pool.display_pool_daos_space(display_string)
                kwargs = {"pool": self.pool.uuid,
                          "cont": self.container.uuid}
                output = self.daos_command.container_check(**kwargs)
                self.log.info(output)

    def test_nvme_pool_excluded(self):
        """Test ID: DAOS-2086.

        Test Description: This method is called from the avocado test infrastructure. This method
            invokes NVME pool exclude testing on multiple pools.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
        :avocado: tags=nvme,checksum,nvme_osa
        :avocado: tags=NvmePoolExclude,test_nvme_pool_excluded
        """
        self.run_nvme_pool_exclude(1)
