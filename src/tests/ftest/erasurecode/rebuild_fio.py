'''
  (C) Copyright 2019-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
import time

from ec_utils import ErasureCodeFio


class EcodFioRebuild(ErasureCodeFio):
    # pylint: disable=too-many-ancestors
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

    def get_pool_freespace(self):
        """Get pool total free space.

        Return:
            free_space (int): pool total free space.
        """
        tier_stats = self.pool.get_tier_stats(True)
        total_freespace = tier_stats["scm"]["free"] + tier_stats["nvme"]["free"]
        self.log.info("==>tier_stats= %s", tier_stats)
        return total_freespace

    def execution(self, rebuild_mode):
        """Execute test.

        Args:
            rebuild_mode (str): On-line or off-line rebuild mode
        """
        aggregation_threshold = self.params.get("aggregation_threshold", "/run/pool/*")
        # 1. Disable aggregation
        self.log.info("==>(1)Disable aggregation")
        self.pool.set_property("reclaim", "disabled")

        # 2.a Kill last server rank first
        self.rank_to_kill = self.server_count - 1

        if 'on-line' in rebuild_mode:
            # Enabled on-line rebuild for the test
            self.set_online_rebuild = True

        # 2.b Write the Fio data and kill server if rebuild_mode is on-line
        self.log.info("==>(2)start fio and kill server")
        self.start_online_fio()

        # 3. Get initial total free space (scm+nvme)
        init_pool_freespace = self.get_pool_freespace()
        self.log.info("==>(3)Before enable aggregation, pool freespace= %d", init_pool_freespace)

        # 4. Enable aggregation
        self.log.info("==>(4)Enable aggregation")
        self.pool.set_property("reclaim", "time")

        # 5. Get total free space (scm+nvme) after aggregation enabled.
        pool_freespace = self.get_pool_freespace()
        self.log.info("==>(5)After enable aggregation, pool freespace= %d", pool_freespace)

        # 6. Verify Aggregation should start for Partial stripes IO
        # wait until aggregation triggered, check for more than a threshold free space released.
        start = time.time()
        max_elapse_time = 360
        retry_timeout = False
        retry = 0
        while not retry_timeout and pool_freespace <= init_pool_freespace + aggregation_threshold:
            time.sleep(10)
            retry += 1
            if time.time() - start > max_elapse_time:
                retry_timeout = True
            pool_freespace = self.get_pool_freespace()
            self.log.info(
                "==>(6.%d)After enable aggregation, pool freespace= %d", retry, pool_freespace)
        if retry_timeout:
            self.fail("Aggregation did not triggered and timeout")

        if 'off-line' in rebuild_mode:
            self.server_managers[0].stop_ranks(
                [self.server_count - 1], self.d_log, force=True)

        # 8. Adding unlink option for final read command
        if int(self.container.properties.value.split(":")[1]) == 1:
            self.fio_cmd._jobs['test'].unlink.value = 1

        # Read and verify the original data.
        self.fio_cmd._jobs['test'].rw.value = self.read_option
        self.fio_cmd.run()

        # 9. If RF is 2 kill one more server and validate the data is not corrupted.
        if int(self.container.properties.value.split(":")[1]) == 2:
            self.fio_cmd._jobs['test'].unlink.value = 1
            self.log.info("RF is 2,So kill another server and verify data")
            # Kill one more server rank
            self.server_managers[0].stop_ranks([self.server_count - 2], self.d_log, force=True)
            # Read and verify the original data.
            self.fio_cmd.run()

    def test_ec_online_rebuild_fio(self):
        """Jira ID: DAOS-7320.

        Test Description:
            Verify the EC works for Fio during on-line rebuild.

        Use Cases (steps):
            0. Create the container with RF:1 or 2.
               Create the Fio data file with verify pattern over Fuse.
            1. Disable aggregation
            2. Kill the server when Write is in progress.
            3. Enable aggregation
            4. Verify the Fio write finish without any error.
            5. Get total space consumed (scm+nvme).
            6. Enable aggregation.
            7. Get total space consumed (scm+nvme) after aggregation, wait for
               maximum 3 minutes until aggregation triggered.
            8. Read and verify the data after Aggregation.
            9. Kill one more rank and verify the data after rebuild finish.

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
