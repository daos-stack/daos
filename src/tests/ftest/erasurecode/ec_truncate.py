#!/usr/bin/python3
'''
  (C) Copyright 2019-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
import os

from fio_test_base import FioBase
from general_utils import run_pcmd, get_remote_file_size

class Ecodtruncate(FioBase):
    # pylint: disable=too-many-ancestors
    # pylint: disable=protected-access
    """Test class Description: Runs Fio with EC object type over POSIX and
        verify truncate file does not corrupt the data.

    :avocado: recursive
    """

    def test_ec_truncate(self):
        """Jira ID: DAOS-7328.

        Test Description:
            Verify the truncate on EC object class works fine over fuse.

        Use Cases:
            Create the container with EC class
            Create the data file with verify pattern over Fuse
            Truncate the file and increase the size
            Verify the data content and file size
            Truncate the file and reduce the size to original
            Verify the data content and file size

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large,ib2
        :avocado: tags=ec,ec_fio,ec_posix
        :avocado: tags=ec_truncate
        """
        truncate_size = int(self.params.get("truncate_size", '/run/fio/*'))
        fname = self.params.get("names", '/run/fio/*')

        # Write the file using Fio
        self.execute_fio(stop_dfuse=False)

        # Get the fuse file name.
        testfile = "{}.0.0".format(os.path.join(self.dfuse.mount_dir.value,
                                                fname[0]))
        original_fs = int(self.fio_cmd._jobs['test'].size.value)

        # Read and verify the original data.
        self.fio_cmd._jobs['test'].rw = 'read'
        self.fio_cmd.run()

        # Get the file stats and confirm size
        file_size = get_remote_file_size(self.hostlist_clients[0], testfile)
        self.assertEqual(original_fs, file_size)

        # Truncate the original file which will extend the size of file.
        result = run_pcmd(self.hostlist_clients, "truncate -s {} {}"
                          .format(truncate_size, testfile))
        if result[0]["exit_status"] == 1:
            self.fail("Failed to truncate file {}".format(testfile))

        # Verify the file size is extended.
        file_size = get_remote_file_size(self.hostlist_clients[0], testfile)
        self.assertEqual(truncate_size, file_size)

        # Read and verify the data after truncate.
        self.fio_cmd.run()

        # Truncate the original file and shrink to original size.
        result = run_pcmd(self.hostlist_clients, "truncate -s {} {}"
                          .format(original_fs, testfile))
        if result[0]["exit_status"] == 1:
            self.fail("Failed to truncate file {}".format(testfile))

        # Verify the file size is shrink to original.
        file_size = get_remote_file_size(self.hostlist_clients[0], testfile)
        self.assertEqual(original_fs, file_size)

        # Read and verify the data after truncate.
        self.fio_cmd.run()
