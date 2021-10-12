#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import os
import errno
from test_utils_container import TestContainer
from daos_utils import DaosCommand

from dfuse_test_base import DfuseTestBase

class Enospace(DfuseTestBase):
    # pylint: disable=too-many-ancestors,too-few-public-methods
    """Dfuse Sparse File base class.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a Enospace object."""
        super().__init__(*args, **kwargs)
        self.space_before = None

    def test_enospace(self):
        """Jira ID: DAOS-8264.

        Test Description:
            This test is intended to test dfuse writes under enospace
            conditions
        Use cases:
            Create Pool
            Create Posix container
            Mount dfuse
            Create file
            Write to file until error occurs
            The test should then get a enospace error.
        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=daosio,dfuse
        :avocado: tags=dfuseenospace
        """
        # Create a pool, container and start dfuse.
        self.add_pool(connect=False)
        container = TestContainer(self.pool, daos_command=DaosCommand(self.bin))
        container.get_params(self)
        container.create()
        self.start_dfuse(self.hostlist_clients, self.pool, container)

        # create large file and perform write to it so that if goes out of
        # space.
        target_file = os.path.join(self.dfuse.mount_dir.value, "file.txt")

        # pylint: disable=consider-using-with
        fd = open(target_file, 'wb', buffering=0)

        # Use a write size of 128.  On CentOS 8 this could be 1MiB, however older kernels use
        # 128k, and using a bigger size here than the kernel can support will lead to the kernel
        # splitting writes, and the size check atfer ENOSPC failing due to writes having
        # partially succeeded.
        write_size = 1024 * 128
        file_size = 0
        while True:
            stat_pre = os.fstat(fd.fileno())
            assert stat_pre.st_size == file_size
            try:
                fd.write(bytearray(write_size))
                file_size += write_size
            except OSError as e:
                if e.errno != errno.ENOSPC:
                    raise
                print('File write returned ENOSPACE')
                stat_post = os.fstat(fd.fileno())
                # Check that the failed write didn't change the file size.
                assert stat_pre.st_size == stat_post.st_size
                break

        fd.close()
        # As the pool is smaller in size there will be no reserved space for metadata
        # so this is expected to fail.
        try:
            os.unlink(target_file)
        except OSError as e:
            if e.errno != errno.ENOSPC:
                raise
