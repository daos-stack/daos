"""
  (C) Copyright 2024 Intel Corporation.
  SPDX-License-Identifier: BSD-2-Clause-Patent
"""


from apricot import TestWithServers
from dfuse_utils import get_dfuse, start_dfuse
from run_utils import run_remote


class DFuseConReadTest(TestWithServers):
    """Base ReadTest test class.
    :avocado: recursive
    """

    def test_dfuse_con_read(self):
        """
        Test Description:
            Launch concurrent reads.

        Create single files, then read in parallel.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=dfuse
        :avocado: tags=DFuseConReadTest,test_dfuse_con_read
        """

        # There are three basic tests here:
        #  Parallel reads of the same file when reading entire file in one go
        #  Parallel reads of the same file in 128k chunks, using dfuse caching
        #  Parallel reads of same file when pre-read is active (contents fetched on open)

        pool = self.get_pool(connect=False)
        container = self.get_container(pool)

        dfuse = get_dfuse(self, self.hostlist_clients)

        start_dfuse(self, dfuse, pool, container)

        rd = f"{dfuse.mount_dir.value}/data/"
        rep_ten = "seq 1 10 | xargs -L 1 -P 100 -I %"

        cmds = [f"mkdir -p {rd}one",
                f"mkdir -p {rd}two",
                f"mkdir -p {rd}three",
                f"dd if=/dev/zero of={rd}one/file bs=64k count=1",
                f"dd if=/dev/zero of={rd}two/file bs=1M count=1",
                f"dd if=/dev/zero of={rd}three/file bs=64k count=1",
                f"dd if=/dev/zero of={rd}three/token bs=1 count=1",
                f"daos filesystem evict {rd}",
                f"{rep_ten} dd if={rd}one/file bs=64k count=1 of=/dev/zero",
                f"{rep_ten} dd if={rd}two/file bs=128k count=1 of=/dev/zero",
                f"dd if={rd}/three/token of=/dev/zero bs=64k count=1",
                f"{rep_ten} dd if={rd}three/file bs=64k count=1 of=/dev/zero"]

        for cmd in cmds:
            result = run_remote(self.log, self.hostlist_clients, cmd)
            if not result.passed:
                self.fail(f'"{cmd}" failed on {result.failed_hosts}')
