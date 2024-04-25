'''
  (C) Copyright 2019-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
import time

from ec_utils import ErasureCodeFio


class EcodFioRebuild(ErasureCodeFio):
    # pylint: disable=protected-access
    """Test class Description: Runs Fio with EC object type over POSIX and
        verify on-line, off-line for rebuild and verify the data.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a EcodFioRebuild object."""
        super().__init__(*args, **kwargs)
        self.set_online_rebuild = False
        self.rank_to_kill = None
        self.read_option = self.params.get("rw_read", "/run/fio/test/read_write/*")

    def execution(self, rebuild_mode):
        """Execute test.

        Args:
            rebuild_mode (str): On-line or off-line rebuild mode
        """
        aggregation_threshold = self.params.get("threshold", "/run/pool/aggregation/*")
        aggregation_timeout = self.params.get("aggr_timeout", "/run/pool/aggregation/*")
        # 1. Disable aggregation
        self.log_step("Disable aggregation")
        self.pool.disable_aggregation()

        # 2.a Kill last server rank first
        self.log_step("Start fio and kill the last server")
        self.rank_to_kill = self.server_count - 1
        if 'on-line' in rebuild_mode:
            # Enabled on-line rebuild for the test
            self.set_online_rebuild = True
        # 2.b Write the Fio data and kill server if rebuild_mode is on-line
        self.start_online_fio(self.pool)

        # 3. Get initial total free space (scm+nvme)
        self.log_step("Get initial total free space (scm+nvme)")
        init_free_space = self.pool.get_total_free_space(refresh=True)

        # 4. Enable aggregation
        self.log_step("Enable aggregation")
        self.pool.enable_aggregation()

        # 5. Get total space consumed (scm+nvme) after aggregation enabled, verify and wait until
        #    aggregation triggered, maximum 3 minutes.
        self.log_step("Verify the Fio write finish without any error")
        start_time = time.time()
        timed_out = False
        aggr_triggered = False
        self.log_step("Verify and wait until aggregation triggered")
        while not aggr_triggered and not timed_out:
            # Check if current free space exceeds threshold
            free_space = self.pool.get_total_free_space(refresh=True)
            difference = free_space - init_free_space
            aggr_triggered = difference >= aggregation_threshold
            self.log.debug("Total Free space: initial=%s, current=%s, difference=%s",
                           "{:,}".format(init_free_space), "{:,}".format(free_space),
                           "{:,}".format(difference))
            # Check timeout
            timed_out = (time.time() - start_time) > aggregation_timeout
            if not aggr_triggered and not timed_out:
                time.sleep(1)
        if timed_out:
            self.fail("Aggregation not observed within {} seconds".format(aggregation_timeout))

        # ec off-line rebuild fio
        if 'off-line' in rebuild_mode:
            self.log_step("Stop rank for ec off-line rebuild fio")
            self.server_managers[0].stop_ranks(
                [self.server_count - 1], self.d_log, force=True)

        # 6. Adding unlink option for final read command
        self.log_step("Adding unlink option for final read command")
        if int(self.container.properties.value.split(":")[1]) == 1:
            self.fio_cmd._jobs['test'].unlink.value = 1

        # 7. Read and verify the original data.
        self.log_step("Read and verify the original data.")
        self.fio_cmd._jobs['test'].rw.value = self.read_option
        self.fio_cmd.run()

        # 8. If RF is 2 kill one more server and validate the data is not corrupted.
        self.log_step("If RF is 2 kill one more server and validate the data is not corrupted.")
        if int(self.container.properties.value.split(":")[1]) == 2:
            self.fio_cmd._jobs['test'].unlink.value = 1
            self.log.info("RF is 2,So kill another server and verify data")
            # Kill one more server rank
            self.server_managers[0].stop_ranks([self.server_count - 2], self.d_log, force=True)
            # Read and verify the original data.
            self.fio_cmd.run()

        # Pre-teardown: make sure rebuild is done before too-quickly trying to destroy container.
        self.pool.wait_for_rebuild_to_end()

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
