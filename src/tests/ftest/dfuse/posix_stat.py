#!/usr/bin/python
"""
  (C) Copyright 2018-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from avocado.core.exceptions import TestFail

from ior_test_base import IorTestBase
from general_utils import get_remote_file_size, run_pcmd


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
        :avocado: tags=vm
        :avocado: tags=dfuse
        :avocado: tags=stat_parameters,test_stat_parameters
        """
        block_sizes = self.params.get("block_sizes", "/run/*")
        error_list = []

        self.add_pool(connect=False)
        self.add_container(pool=self.pool)

        i = 1
        for block_size in block_sizes:
            self.log.info("Block Size = %s", block_size)
            self.ior_cmd.block_size.update(block_size)

            # 1. Verify creation time.
            test_file_suffix = "_{}".format(i)
            i += 1

            # Run ior command.
            try:
                self.run_ior_with_pool(
                    timeout=200, stop_dfuse=False, create_pool=False,
                    create_cont=False, test_file_suffix=test_file_suffix)
            except TestFail:
                self.log.info("ior command failed!")

            # Get current epoch.
            current_epoch = -1
            output = run_pcmd(hosts=self.hostlist_clients, command="date +%s")
            stdout = output[0]["stdout"]
            self.log.info("date stdout = %s", stdout)
            current_epoch = stdout[-1]

            # Get epoch of the created file. (technically %Z is for last status
            # change. %W is file birth, but it returns 0.)
            creation_epoch = -1
            # As in date command, run stat command in the client node.
            stat_command = "stat -c%Z {}".format(self.ior_cmd.test_file.value)
            output = run_pcmd(hosts=self.hostlist_clients, command=stat_command)
            stdout = output[0]["stdout"]
            self.log.info("stat stdout = %s", stdout)
            creation_epoch = stdout[-1]

            # Calculate the epoch difference between the creation time and the
            # value in the file metadata. They're usually 2 sec apart.
            creation_epoch_int = int(creation_epoch)
            current_epoch_int = int(current_epoch)
            diff_epoch = creation_epoch_int - current_epoch_int
            if diff_epoch > 10:
                msg = "Unexpected creation time! Expected = {}; Actual = {}"
                error_list.append(
                    msg.format(current_epoch_int, creation_epoch_int))

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
