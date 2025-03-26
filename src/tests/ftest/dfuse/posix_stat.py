"""
  (C) Copyright 2018-2023 Intel Corporation.
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

from general_utils import get_remote_file_size
from ior_test_base import IorTestBase
from run_utils import run_remote


class POSIXStatTest(IorTestBase):
    """Test class description:

    Requirement: SRS-10-0303
    DFS stat call reports meaningful information (i.e. valid file size, correct
    block size, ...).

    Test Steps:
    Create files of different size (block size) in a mounted POSIX container and
    verify the size with stat command. Also verify the creation time with date
    command.

    :avocado: recursive
    """

    def test_stat_parameters(self):
        """JIRA ID: DAOS-3769

        Create files of 1M, 10M, 100M, 500M, and verify the size and creation
        time.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=dfuse
        :avocado: tags=POSIXStatTest,test_stat_parameters
        """
        block_sizes = self.params.get("block_sizes", "/run/*")
        error_list = []

        self.add_pool(connect=False)
        self.add_container(pool=self.pool)

        idx = 1
        for block_size in block_sizes:
            self.log.info("Block Size = %s", block_size)
            self.ior_cmd.block_size.update(block_size)

            # 1. Verify creation time.
            test_file_suffix = "_{}".format(idx)
            idx += 1

            # Run ior command.
            self.run_ior_with_pool(
                timeout=200, stop_dfuse=False, create_pool=False,
                create_cont=False, test_file_suffix=test_file_suffix)

            # Get current epoch.
            result = run_remote(self.log, self.hostlist_clients, "date +%s")
            if not result.passed:
                self.fail("Failed to get date on clients")
            current_epoch = int(result.output[0].stdout[-1])

            # Get epoch of the created file. (technically %Z is for last status
            # change. %W is file birth, but it returns 0.)
            # As in date command, run stat command in the client node.
            stat_command = "stat -c%Z {}".format(self.ior_cmd.test_file.value)
            result = run_remote(self.log, self.hostlist_clients, stat_command)
            if not result.passed:
                self.fail(f"{stat_command} failed on clients")
            creation_epoch = int(result.output[0].stdout[-1])

            # Calculate the epoch difference between the creation time and the
            # value in the file metadata. They're usually 2 sec apart.
            diff_epoch = creation_epoch - current_epoch
            if diff_epoch > 10:
                msg = "Unexpected creation time! Expected = {}; Actual = {}"
                error_list.append(
                    msg.format(current_epoch, creation_epoch))

            # 2. Verify file size.
            # Get file size.
            file_size = get_remote_file_size(
                self.hostlist_clients[0], self.ior_cmd.test_file.value)

            # Adjust the file size and verify that it matches the expected size.
            expected_size = block_size[:-1]
            # Obtained size is in byte, so convert it to MB.
            file_size_adjusted = file_size / 1024 / 1024
            if int(expected_size) != file_size_adjusted:
                msg = "Unexpected file size! Expected = {}; Actual = {}"
                error_list.append(
                    msg.format(int(expected_size), file_size_adjusted))

        if error_list:
            self.fail("\n----- Errors detected! -----\n{}".format(
                "\n".join(error_list)))
