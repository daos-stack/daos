"""
  (C) Copyright 2020-2024 Intel Corporation.
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os

from dfuse_utils import get_dfuse, start_dfuse
from ior_test_base import IorTestBase
from run_utils import run_remote


class DfuseSpaceCheck(IorTestBase):
    """DfuseSpaceCheck test class.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a DfuseSpaceCheck object."""
        super().__init__(*args, **kwargs)
        self.__initial_space = None
        self.__block_size = None

    def get_nvme_free_space(self, display=True):
        """Display pool free space.

        Args:
            display (bool): boolean to display output of free space.

        Returns:
            int: Free space available in nvme.

        """
        free_space_nvme = self.pool.get_pool_free_space("nvme")
        if display:
            self.log.info("Free nvme space: %s", free_space_nvme)

        return free_space_nvme

    def wait_for_aggregation(self):
        """Wait for aggregation to finish."""
        if not self.pool.check_free_space(
                expected_nvme=self.__initial_space, timeout=240, interval=30):
            self.fail("Aggregation did not complete within 240 seconds")

    def write_multiple_files(self, dfuse):
        """Write multiple files.

        Args:
            dfuse (Dfuse): the dfuse object

        Returns:
            int: Total number of files created before going out of space.

        """
        file_count = 0
        while self.get_nvme_free_space(False) >= self.__block_size:
            file_path = os.path.join(dfuse.mount_dir.value, "file{}.txt".format(file_count))
            write_dd_cmd = f"dd if=/dev/zero of={file_path} bs={self.__block_size} count=1"
            result = run_remote(
                self.log, self.hostlist_clients, write_dd_cmd, verbose=False, timeout=300)
            if not result.passed:
                self.fail(f"Error running: {write_dd_cmd}")
            file_count += 1

        return file_count

    def test_dfuse_space_check(self):
        """Jira ID: DAOS-3777.

        Test Description:
            Purpose of this test is to mount dfuse and verify aggregation to return space when pool
            is filled with once large file and once with small files.

        Use cases:
            Create a pool.
            Create a POSIX container.
            Mount dfuse.
            Write to a large file until the pool is out of space.
            Remove the file and wait for aggregation to reclaim the space.
            Disable aggregation.
            Create small files until the pool is out of space.
            Enable aggregation.
            Remove the files and wait for aggregation to reclaim the space.
            Disable aggregation.
            Create small files until the pool is out of space.
            Verify the same number of files were written.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=aggregation,daosio,dfuse
        :avocado: tags=DfuseSpaceCheck,test_dfuse_space_check
        """
        # Get test params for cont and pool count
        self.__block_size = self.params.get('block_size', '/run/dfuse_space_check/*')

        # Create a pool, container, and start dfuse
        self.create_pool()
        self.create_cont()
        dfuse = get_dfuse(self, self.hostlist_clients)
        start_dfuse(self, dfuse, self.pool, self.container)

        # Get nvme space before write
        self.__initial_space = self.get_nvme_free_space()

        # Create a file as large as we can
        large_file = os.path.join(dfuse.mount_dir.value, 'largefile.txt')
        if not run_remote(self.log, self.hostlist_clients, f'touch {large_file}').passed:
            self.fail(f"Error creating {large_file}")
        dd_count = (self.__initial_space // self.__block_size) + 1
        write_dd_cmd = "dd if=/dev/zero of={} bs={} count={}".format(
            large_file, self.__block_size, dd_count)
        run_remote(self.log, self.hostlist_clients, write_dd_cmd)

        # Remove the file
        if not run_remote(self.log, self.hostlist_clients, f'rm -rf {large_file}').passed:
            self.fail(f"Error removing {large_file}")

        # Wait for aggregation to complete
        self.wait_for_aggregation()

        # Disable aggregation
        self.log.info("Disabling aggregation")
        self.pool.set_property("reclaim", "disabled")

        # Write small files until we run out of space
        file_count1 = self.write_multiple_files(dfuse)

        # Enable aggregation
        self.log.info("Enabling aggregation")
        self.pool.set_property("reclaim", "time")

        # remove all the small files created above.
        result = run_remote(
            self.log, self.hostlist_clients, f"rm -rf {os.path.join(dfuse.mount_dir.value, '*')}")
        if not result.passed:
            self.fail("Error removing files in mount dir")

        # Wait for aggregation to complete after file removal
        self.wait_for_aggregation()

        # Disable aggregation
        self.log.info("Disabling aggregation")
        self.pool.set_property("reclaim", "disabled")

        # Write small files again until we run out of space and verify we wrote the same amount
        file_count2 = self.write_multiple_files(dfuse)

        self.log.info('file_count1 = %s', file_count1)
        self.log.info('file_count2 = %s', file_count2)
        self.assertEqual(
            file_count2, file_count1,
            'Space was not returned. Expected to write the same number of files')
