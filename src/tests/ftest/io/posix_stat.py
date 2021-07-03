#!/usr/bin/python
"""
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from avocado.core.exceptions import TestFail

from ior_test_base import IorTestBase
from general_utils import get_remote_file_size, run_task


class POSIXStatTest(IorTestBase):
    # pylint: disable=too-many-ancestors
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
        :avocado: tags=stat_parameters
        """
        block_sizes = self.params.get("block_sizes", "/run/*")
        testfile_path = "/tmp/daos_dfuse/testfile"
        error_list = []

        self.add_pool(connect=False)
        self.add_container(pool=self.pool)

        for block_size in block_sizes:
            self.log.info("Block Size = %s", block_size)
            self.ior_cmd.block_size.update(block_size)

            # 1. Verify creation time.
            # Get current epoch before running ior. The timestamp of the file
            # created by ior is near the time of the start of the ior command
            # execution.
            current_epoch = -1
            cmd_output = ""
            # Run date command in the client node because that's where the file
            # is created.
            task = run_task(self.hostlist_clients, "date +%s")
            for output, _ in task.iter_buffers():
                cmd_output = str(output)
            current_epoch = cmd_output.split()[-1]

            # Run ior command.
            try:
                self.run_ior_with_pool(timeout=200, stop_dfuse=False)
            except TestFail:
                self.log.info("ior failed! " + str(self.ior_cmd))

            # Get epoch of the created file.
            creation_epoch = -1
            cmd_output = ""
            # As in date command, run stat command in the client node.
            task = run_task(
                self.hostlist_clients, "stat -c%Z {}".format(testfile_path))
            for output, _ in task.iter_buffers():
                cmd_output = str(output)
            # The output may contain some warning messages, so use split to get
            # the value we want.
            creation_epoch = cmd_output.split()[-1]

            # Calculate the epoch difference between the creation time and the
            # value in the file metadata. They're usually 5 to 10 sec apart.
            creation_epoch_int = int(creation_epoch)
            current_epoch_int = int(current_epoch)
            diff_epoch = creation_epoch_int - current_epoch_int
            if diff_epoch > 20:
                msg = "Unexpected creation time! Expected = {}; Actual = {}"
                error_list.append(
                    msg.format(current_epoch_int, creation_epoch_int))

            # 2. Verify file size.
            # Get file size.
            file_size = get_remote_file_size(
                self.hostlist_clients[0], testfile_path)

            # Adjust the file size and verify that it matches the expected size.
            expected_size = block_size[:-1]
            # Obtained size is in byte, so convert it to MB.
            file_size_adjusted = file_size / 1024 / 1024
            if int(expected_size) != file_size_adjusted:
                msg = "Unexpected file size! Expected = {}; Actual = {}"
                error_list.append(
                    msg.format(int(expected_size), file_size_adjusted))

        # Manually stop dfuse because we set stop_dfuse=False in
        # run_ior_with_pool.
        self.stop_dfuse()

        if error_list:
            self.fail("\n".join(error_list))
