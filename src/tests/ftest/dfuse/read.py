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

    def test_bashfd(self):
        """

        Test Description:
            Run a simple Write/Read test to check for read caching.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=dfuse,dfs
        :avocado: tags=DFuseReadTest,test_dfuse_read
        """

        pool = self.get_pool(connect=False)
        container = self.get_container(pool)

        self.dfuse.disable_wb_cache.value = True

        cont_attrs = {}

        cont_attrs['dfuse-data-cache'] = '1h'
        cont_attrs['dfuse-attr-time'] = '1h'
        cont_attrs['dfuse-dentry-time'] = '1h'
        cont_attrs['dfuse-ndentry-time'] = '1h'

        self.container.set_attr(attrs=cont_attrs)

        self.start_dfuse(self.hostlist_clients, pool, container)

        fuse_root_dir = self.dfuse.mount_dir.value

        cmd = f"dd if=/dev/zero of={fuse_root_dir}/test_file count=10 bs=1"

        result = run_remote(self.log, self.hostlist_clients, cmd)
        if not result.passed:
            self.fail(f'"{cmd}" failed on {result.failed_hosts}')

        cmd = f"daos filesystem query {fuse_root_dir}"
        result = run_remote(self.log, self.hostlist_clients, cmd)
        if not result.passed:
            self.fail(f'"{cmd}" failed on {result.failed_hosts}')

        cmd = f"dd if={fuse_root_dir}/test_file of=/dev/zero count=10 bs=1"

        result = run_remote(self.log, self.hostlist_clients, cmd)
        if not result.passed:
            self.fail(f'"{cmd}" failed on {result.failed_hosts}')

        cmd = f"daos filesystem query {fuse_root_dir}"
        result = run_remote(self.log, self.hostlist_clients, cmd)
        if not result.passed:
            self.fail(f'"{cmd}" failed on {result.failed_hosts}')
