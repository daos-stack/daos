'''
  (C) Copyright 2019-2024 Intel Corporation.
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
import os

from dfuse_utils import get_dfuse, start_dfuse
from fio_test_base import FioBase
from general_utils import get_remote_file_size
from run_utils import run_remote


class Ecodtruncate(FioBase):
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
        :avocado: tags=hw,large
        :avocado: tags=ec,ec_fio,ec_posix,fio
        :avocado: tags=Ecodtruncate,test_ec_truncate
        """
        truncate_size = int(self.params.get("truncate_size", '/run/fio/*'))
        fname = self.params.get("names", '/run/fio/*')

        # Write the file using Fio
        pool = self.get_pool(connect=False)
        container = self.get_container(pool)
        container.set_attr(attrs={'dfuse-direct-io-disable': 'on'})
        dfuse = get_dfuse(self, self.hostlist_clients)
        start_dfuse(self, dfuse, pool, container)
        self.fio_cmd.update_directory(dfuse.mount_dir.value)
        self.execute_fio()

        # Get the fuse file name.
        testfile = "{}.0.0".format(os.path.join(dfuse.mount_dir.value, fname[0]))
        original_fs = int(self.fio_cmd._jobs['test'].size.value)

        # Read and verify the original data.
        self.fio_cmd._jobs['test'].rw = 'read'
        self.fio_cmd.run()

        # Get the file stats and confirm size
        file_size = get_remote_file_size(self.hostlist_clients[0], testfile)
        self.assertEqual(
            original_fs, file_size, "file size after truncase is not equal to original")

        # Truncate the original file which will extend the size of file.
        result = run_remote(
            self.log, self.hostlist_clients, f"truncate -s {truncate_size} {testfile}")
        if not result.passed:
            self.fail(f"Failed to truncate file {testfile}")

        # Verify the file size is extended.
        file_size = get_remote_file_size(self.hostlist_clients[0], testfile)
        self.assertEqual(truncate_size, file_size)

        # Read and verify the data after truncate.
        self.fio_cmd.run()

        # Truncate the original file and shrink to original size.
        result = run_remote(
            self.log, self.hostlist_clients, f"truncate -s {original_fs} {testfile}")
        if not result.passed:
            self.fail(f"Failed to truncate file {testfile}")

        # Verify the file size is shrink to original.
        file_size = get_remote_file_size(self.hostlist_clients[0], testfile)
        self.assertEqual(
            original_fs, file_size, "file size after truncase is not equal to original")

        # Read and verify the data after truncate.
        self.fio_cmd.run()
