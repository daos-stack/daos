"""
  (C) Copyright 2024 Intel Corporation.
  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import time

from dfuse_test_base import DfuseTestBase
from run_utils import run_remote


class DFusePreReadTest(DfuseTestBase):
    """Base ReadTest test class.
    :avocado: recursive
    """

    def test_dfuse_pre_read(self):
        """
        Test Description:
            Ensure that pre-read feature is working.
        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=dfuse
        :avocado: tags=DFusePreReadTest,test_dfuse_pre_read
        """

        pool = self.get_pool(connect=False)
        container = self.get_container(pool)

        self.load_dfuse(self.hostlist_clients, None)

        cont_attrs = {}

        cont_attrs["dfuse-data-cache"] = "1h"
        cont_attrs["dfuse-attr-time"] = "1h"
        cont_attrs["dfuse-dentry-time"] = "1h"
        cont_attrs["dfuse-ndentry-time"] = "1h"

        container.set_attr(attrs=cont_attrs)

        self.start_dfuse(self.hostlist_clients, pool, container)

        fuse_root_dir = self.dfuse.mount_dir.value

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

        # Instruct dfuse to forget the directory and therefore file.
        cmd = f"daos fs evict {fuse_root_dir}/td"
        result = run_remote(self.log, self.hostlist_clients, cmd)
        if not result.passed:
            self.fail(f'"{cmd}" failed on {result.failed_hosts}')

        # Allow the eviction to happen.  It should be nearly instant and much quicker than launching
        # commands via ssh but there's no harm in adding this.
        time.sleep(1)

        # Sample the stats, later on we'll check this.
        data = self.dfuse.get_stats()

        # Check that the inode has been evicted, and there's been no reads so far.
        self.assertEqual(data["inodes"], 1, "Incorrect number of active nodes")
        self.assertEqual(data["statistics"].get("read", 0), 0, "expected zero reads")
        self.assertEqual(
            data["statistics"].get("pre_read", 0), 0, "expected zero pre reads"
        )

        # Now read the file, and check it's read.
        cmd = f"dd if={fuse_root_dir}/td/test_file of=/dev/zero count=16 bs=128k"
        result = run_remote(self.log, self.hostlist_clients, cmd)
        if not result.passed:
            self.fail(f'"{cmd}" failed on {result.failed_hosts}')

        data = self.dfuse.get_stats()

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

        self.assertEqual(data["inodes"], 3, "expected 3 inodes in cache")


class DFuseReadTest(DfuseTestBase):
    """Base ReadTest test class.

    :avocado: recursive
    """

    def test_dfuse_read(self):
        """
        Test Description:
            Run a simple Write/Read test to check for read caching.

        Write a file, then read from it and verify that there were no reads at the dfuse level.

        Evict the file, read from it twice and verify the second read comes from cache.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=dfuse,dfs
        :avocado: tags=DFuseReadTest,test_dfuse_read
        """

        pool = self.get_pool(connect=False)
        container = self.get_container(pool)

        self.load_dfuse(self.hostlist_clients, None)

        self.dfuse.disable_wb_cache.value = True

        self.dfuse.env["D_LOG_MASK"] = "INFO,DFUSE=DEBUG"
        self.dfuse.env["DD_MASK"] = "ALL"
        self.dfuse.env["DD_SUBSYS"] = "ALL"

        cont_attrs = {}

        cont_attrs["dfuse-data-cache"] = "1h"
        cont_attrs["dfuse-attr-time"] = "1h"
        cont_attrs["dfuse-dentry-time"] = "1h"
        cont_attrs["dfuse-ndentry-time"] = "1h"

        container.set_attr(attrs=cont_attrs)

        self.start_dfuse(self.hostlist_clients, pool, container)

        fuse_root_dir = self.dfuse.mount_dir.value

        cmd = f"dd if=/dev/zero of={fuse_root_dir}/test_file count=16 bs=1M"
        result = run_remote(self.log, self.hostlist_clients, cmd)
        if not result.passed:
            self.fail(f'"{cmd}" failed on {result.failed_hosts}')

        cmd = f"dd if={fuse_root_dir}/test_file of=/dev/zero count=16 bs=1M"
        result = run_remote(self.log, self.hostlist_clients, cmd)
        if not result.passed:
            self.fail(f'"{cmd}" failed on {result.failed_hosts}')

        data = self.dfuse.get_stats()

        read_calls = data["statistics"].get("read", 0)
        write_calls = data["statistics"].get("write")

        print(f"Test caused {write_calls} write and {read_calls} reads calls")

        self.assertEqual(data["statistics"].get("read", 0), 0, "Did not expect any read calls")

        cmd = f"daos filesystem evict {fuse_root_dir}/test_file"
        result = run_remote(self.log, self.hostlist_clients, cmd)
        if not result.passed:
            self.fail(f'"{cmd}" failed on {result.failed_hosts}')

        cmd = f"dd if={fuse_root_dir}/test_file of=/dev/zero count=16 bs=1M"
        result = run_remote(self.log, self.hostlist_clients, cmd)
        if not result.passed:
            self.fail(f'"{cmd}" failed on {result.failed_hosts}')

        data = self.dfuse.get_stats()

        self.assertGreater(
            data["statistics"].get("read", 0), 0, "expected non-zero pre read"
        )

        result = run_remote(self.log, self.hostlist_clients, cmd)
        if not result.passed:
            self.fail(f'"{cmd}" failed on {result.failed_hosts}')

        data2 = self.dfuse.get_stats()

        self.assertEqual(data["statistics"].get("read", 0), data2["statistics"].get("read", 0), "Did not expect more read calls")

