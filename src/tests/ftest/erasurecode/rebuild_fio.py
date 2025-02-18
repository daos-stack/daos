'''
  (C) Copyright 2019-2024 Intel Corporation.
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
import queue
import threading
import time

from dfuse_utils import get_dfuse, start_dfuse
from fio_test_base import FioBase


class EcodFioRebuild(FioBase):
    """Test class Description: Runs Fio with EC object type over POSIX and
        verify on-line, off-line for rebuild and verify the data.

    :avocado: recursive
    """

    def execution(self, rebuild_mode):
        """Execute test.

        Args:
            rebuild_mode (str): On-line or off-line rebuild mode
        """
        aggregation_timeout = self.params.get("aggr_timeout", "/run/pool/aggregation/*")
        read_option = self.params.get("rw_read", "/run/fio/test/read_write/*")

        num_ranks = len(self.server_managers[0].ranks)
        rank_to_kill = num_ranks - 1

        # 1. Disable aggregation
        self.log_step("Disable aggregation")
        pool = self.get_pool()
        pool.disable_aggregation()

        # Start dfuse
        self.log_step('Starting dfuse')
        container = self.get_container(pool)
        container.set_attr(attrs={'dfuse-direct-io-disable': 'on'})
        dfuse = get_dfuse(self, self.hostlist_clients)
        start_dfuse(self, dfuse, pool, container)

        # Write the Fio data and kill the last server rank if rebuild_mode is on-line
        if 'on-line' in rebuild_mode:
            self.log_step(f"Start fio and stop the last server rank ({rank_to_kill})")
            self.start_online_fio(dfuse.mount_dir.value, rank_to_kill)
        else:
            self.log_step("Start fio and leave all servers running")
            self.start_online_fio(dfuse.mount_dir.value, None)

        # Get initial total free space (scm+nvme)
        self.log_step("Get initial total free space (scm+nvme)")
        initial_free_space = pool.get_total_free_space(refresh=True)

        # Enable aggregation
        self.log_step("Enable aggregation")
        pool.enable_aggregation()

        # Wait for aggregation to be triggered.
        # Assume an increase in total free space means aggregation is triggered.
        self.log_step("Verify the Fio write finish without any error")
        start_time = time.time()
        self.log_step("Verify and wait until aggregation triggered")
        while True:
            # Check if current free space exceeds initial free space
            current_free_space = pool.get_total_free_space(refresh=True)
            self.log.debug(
                "Total Free space: initial=%s, current=%s",
                "{:,}".format(initial_free_space), "{:,}".format(current_free_space))
            if current_free_space > initial_free_space:
                break
            # Check timeout
            if (time.time() - start_time) > aggregation_timeout:
                self.fail(f"Aggregation not observed within {aggregation_timeout} seconds")
            self.log.debug("Rechecking in 5 seconds")
            time.sleep(5)

        # ec off-line rebuild fio
        if 'off-line' in rebuild_mode:
            self.log_step(f"Stop the last server rank ({rank_to_kill}) for ec off-line rebuild fio")
            self.server_managers[0].stop_ranks([rank_to_kill], force=True)

        # Adding unlink option for final read command
        self.log_step("Adding unlink option for final read command")
        if int(container.properties.value.split(":")[1]) == 1:
            self.fio_cmd._jobs['test'].unlink.value = 1         # pylint: disable=protected-access

        # Read and verify the original data.
        self.log_step("Read and verify the original data.")
        self.fio_cmd._jobs['test'].rw.value = read_option       # pylint: disable=protected-access
        self.fio_cmd.run()

        # If RF is 2 kill one more server and validate the data is not corrupted.
        if int(container.properties.value.split(":")[1]) == 2:
            # Kill one more server rank
            rank_to_kill = num_ranks - 2
            self.log_step(f"Kill one more server rank {rank_to_kill} when RF=2")
            self.fio_cmd._jobs['test'].unlink.value = 1         # pylint: disable=protected-access
            self.server_managers[0].stop_ranks([rank_to_kill], force=True)

            # Read and verify the original data.
            self.log_step(f"Verify the data is not corrupted after stopping rank {rank_to_kill}.")
            self.fio_cmd.run()

        # Pre-teardown: make sure rebuild is done before too-quickly trying to destroy container.
        pool.wait_for_rebuild_to_end()

        self.log.info("Test passed")

    def start_online_fio(self, directory, rank_to_kill=None):
        """Run Fio operation with thread in background.

        Trigger the server failure while Fio is running

        Args:
            directory (str): directory to use with the fio command
            rank_to_kill (int, optional): the server rank to kill while IO operation is in progress.
                Set to None to leave all servers running during IO. Defaults to None.
        """
        results_queue = queue.Queue()

        # Create the Fio run thread
        job = threading.Thread(
            target=self.write_single_fio_dataset,
            kwargs={"directory": directory, "results": results_queue})

        # Launch the Fio thread
        job.start()

        # Kill the server rank while IO operation in progress
        if rank_to_kill is not None:
            time.sleep(30)
            self.server_managers[0].stop_ranks([rank_to_kill], force=True)

        # Wait to finish the thread
        job.join()

        # Verify the queue result and make sure test has no failure
        while not results_queue.empty():
            if results_queue.get() == "FAIL":
                self.fail("Error running fio as a thread")

    def write_single_fio_dataset(self, directory, results):
        """Run Fio Benchmark.

        Args:
            directory (str): directory to use with the fio command
            results (queue): queue for returning thread results
        """
        try:
            self.fio_cmd.update_directory(directory)
            self.execute_fio()
            results.put("PASS")
        except Exception:       # pylint: disable=broad-except
            results.put("FAIL")

    def test_ec_online_rebuild_fio(self):
        """Jira ID: DAOS-7320.

        Test Description:
            Verify the EC works for Fio during on-line rebuild.

        Use Cases (steps):
            0. Create the container with RF:1 or 2.
               Create the Fio data file with verify pattern over Fuse.
            1. Disable aggregation
            2. Kill the server when Write is in progress.
            3. get total space consumed (scm+nvme)
            4. Enable aggregation
            6. Get total space consumed (scm+nvme) after aggregation enabled, wait until
               aggregation triggered, maximum 3 minutes.
            6. Adding unlink option for final read command
               Read and verify the data after Aggregation.
            7. Verify the Fio write finish without any error.
            8. Kill one more rank and verify the data after rebuild finish.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
        :avocado: tags=ec,ec_array,fio,ec_online_rebuild
        :avocado: tags=EcodFioRebuild,test_ec_online_rebuild_fio
        """
        self.execution('on-line')

    def test_ec_offline_rebuild_fio(self):
        """Jira ID: DAOS-7320.

        Test Description:
            Verify the EC works for Fio, for off-line rebuild.

        Use Cases:
            Create the container with RF:1 or 2.
            Create the Fio data file with verify pattern over Fuse.
            Kill the server and wait for rebuild to finish.
            Wait and verify Aggregation is getting triggered.
            Kill one more rank and verify the data after rebuild finish.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
        :avocado: tags=ec,ec_array,fio,ec_offline_rebuild
        :avocado: tags=EcodFioRebuild,test_ec_offline_rebuild_fio
        """
        self.execution('off-line')
