"""
  (C) Copyright 2024 Intel Corporation.
  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

from dfuse_test_base import DfuseTestBase
from run_utils import run_remote


class DFuseReadTest(DfuseTestBase):
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
        :avocado: tags=DFuseReadTest,test_dfuse_pre_read
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

        # Create the file.
        cmd = f"dd if=/dev/zero of={fuse_root_dir}/test_file count=2 bs=1M"
        result = run_remote(self.log, self.hostlist_clients, cmd)
        if not result.passed:
            self.fail(f'"{cmd}" failed on {result.failed_hosts}')

        # Instruct dfuse to forget the file.
        cmd = f"daos fs evict {fuse_root_dir}/test_file"
        result = run_remote(self.log, self.hostlist_clients, cmd)
        if not result.passed:
            self.fail(f'"{cmd}" failed on {result.failed_hosts}')

        # Sample the stats, later on we'll check this.
        data = self.dfuse.get_stats()
        print(data)

        # Now read the file, and check it's read.
        cmd = f"dd if={fuse_root_dir}/test_file of=/dev/zero count=2 bs=1M"
        result = run_remote(self.log, self.hostlist_clients, cmd)
        if not result.passed:
            self.fail(f'"{cmd}" failed on {result.failed_hosts}')

        data = self.dfuse.get_stats()
        print(data)

        read_calls = data["statistics"].get("read", 0)
        write_calls = data["statistics"].get("write", 0)

        print(f"Test caused {write_calls} write and {read_calls} reads calls")
