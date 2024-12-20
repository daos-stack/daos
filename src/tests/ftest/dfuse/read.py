"""
  (C) Copyright 2024 Intel Corporation.
  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import time

from apricot import TestWithServers
from dfuse_utils import get_dfuse, start_dfuse
from run_utils import run_remote


class DFusePreReadTest(TestWithServers):
    """Base ReadTest test class.
    :avocado: recursive
    """

    def test_dfuse_pre_read(self):
        """
        Test Description:
            Ensure that pre-read feature is working.

        Read one large file entirely using pre-read.  Read a second smaller file to ensure that
        the first file leaves the flag enabled.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=dfuse
        :avocado: tags=DFusePreReadTest,test_dfuse_pre_read
        """

        pool = self.get_pool(connect=False)
        container = self.get_container(pool)

        dfuse = get_dfuse(self, self.hostlist_clients)

        cont_attrs = {}

        cont_attrs["dfuse-data-cache"] = "1h"
        cont_attrs["dfuse-attr-time"] = "1h"
        cont_attrs["dfuse-dentry-time"] = "1h"
        cont_attrs["dfuse-ndentry-time"] = "1h"

        container.set_attr(attrs=cont_attrs)

        start_dfuse(self, dfuse, pool, container)

        fuse_root_dir = dfuse.mount_dir.value

        # make a directory to run the test from.  Pre-read is based on previous access to a
        # directory so this needs to be evicted after the write and before the test so the
        # directory appears "new"
        cmd = f"mkdir {fuse_root_dir}/td"
        result = run_remote(self.log, self.hostlist_clients, cmd)
        if not result.passed:
            self.fail(f'"{cmd}" failed on {result.failed_hosts}')

        # Create the file.
        cmd = f"dd if=/dev/zero of={fuse_root_dir}/td/test_file count=2 bs=1M"
        result = run_remote(self.log, self.hostlist_clients, cmd)
        if not result.passed:
            self.fail(f'"{cmd}" failed on {result.failed_hosts}')

        # Create the second, smaller file.
        cmd = f"dd if=/dev/zero of={fuse_root_dir}/td/test_file2 count=1 bs=1k"
        result = run_remote(self.log, self.hostlist_clients, cmd)
        if not result.passed:
            self.fail(f'"{cmd}" failed on {result.failed_hosts}')

        # Instruct dfuse to forget the directory and therefore file.
        cmd = f"daos fs evict {fuse_root_dir}/td"
        result = run_remote(self.log, self.hostlist_clients, cmd)
        if not result.passed:
            self.fail(f'"{cmd}" failed on {result.failed_hosts}')

        # Allow the eviction to happen.  It should be nearly instant and much quicker than launching
        # commands via ssh but there's no harm in adding this.
        time.sleep(1)

        # Sample the stats, later on we'll check this.
        data = dfuse.get_stats()

        # Check that the inode has been evicted, and there's been no reads so far.
        self.assertEqual(data["inodes"], 1, "Incorrect number of active nodes")
        self.assertEqual(data["statistics"].get("read", 0), 0, "expected zero reads")
        self.assertEqual(
            data["statistics"].get("pre_read", 0), 0, "expected zero pre reads"
        )

        # Now read the file, and check it's read.
        cmd = f"dd if={fuse_root_dir}/td/test_file of=/dev/zero count=1 bs=2M"
        result = run_remote(self.log, self.hostlist_clients, cmd)
        if not result.passed:
            self.fail(f'"{cmd}" failed on {result.failed_hosts}')

        data = dfuse.get_stats()

        # pre_read requests are a subset of reads so for this test we should verify that they are
        # equal, and non-zero.
        self.assertGreater(
            data["statistics"].get("pre_read", 0), 0, "expected non-zero pre read"
        )
        self.assertEqual(
            data["statistics"].get("pre_read"),
            data["statistics"].get("read", 0),
            "pre read does not match read",
        )

        # Now read the smaller file, and check it's read.
        cmd = f"dd if={fuse_root_dir}/td/test_file2 of=/dev/zero bs=1"
        result = run_remote(self.log, self.hostlist_clients, cmd)
        if not result.passed:
            self.fail(f'"{cmd}" failed on {result.failed_hosts}')

        data = dfuse.get_stats()

        # pre_read requests are a subset of reads so for this test we should verify that they are
        # equal, and non-zero.
        self.assertGreater(
            data["statistics"].get("pre_read", 0), 0, "expected non-zero pre read"
        )
        self.assertEqual(
            data["statistics"].get("pre_read"),
            data["statistics"].get("read", 0),
            "pre read does not match read",
        )

        self.assertEqual(data["inodes"], 4, "expected 4 inodes in cache")
