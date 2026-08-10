"""
  (C) Copyright 2024 Intel Corporation.
  (C) Copyright 2026 Hewlett Packard Enterprise Development LP
  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import os
import time

from apricot import TestWithServers
from dfuse_utils import get_dfuse, start_dfuse
from run_utils import run_remote


def helper_failure(label, result):
    """Describe a failed helper run using the helper's own error message.

    Args:
        label (str): which leg of the test failed
        result (CommandResult): the failed run_remote result

    Returns:
        str: the label plus the helper's error output
    """
    data = result.output[0]
    message = "; ".join(data.stdout + data.stderr) or f"helper exited {data.returncode}"
    return f"{label}: {message}"


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

    def test_dfuse_pre_read_stale_eof(self):
        """
        Test Description:
            Guard against DAOS-18683: a read drained while a pre-read RPC was in flight could
            leave a handle claiming EOF at offset zero, so a later read there returned no data
            (plain read) or faulted with SIGBUS (mmap).

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=vm
        :avocado: tags=dfuse
        :avocado: tags=DFusePreReadTest,test_dfuse_pre_read_stale_eof
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
        helper = os.path.join(self.prefix, "lib/daos/TESTING/tests/preread_stale_eof")

        def run_or_fail(cmd):
            result = run_remote(self.log, self.hostlist_clients, cmd)
            if not result.passed:
                self.fail(f'"{cmd}" failed on {result.failed_hosts}')

        def evict_and_wait(subdir, baseline):
            # Eviction is asynchronous; a check run before the forget lands is vacuous.
            run_or_fail(f"daos fs evict {subdir}")
            for _ in range(20):
                if dfuse.get_stats()["inodes"] <= baseline:
                    return
                time.sleep(0.5)
            self.fail(f"eviction of {subdir} did not complete")

        pre_read_before = dfuse.get_stats()["statistics"].get("pre_read", 0)

        sizes = (4096, 65536, 131072, 1024 * 1024)
        iterations = 4
        failures = []
        for itr in range(iterations):
            for size in sizes:
                subdir = f"{fuse_root_dir}/dir.{itr}.{size}"
                target = f"{subdir}/target"
                baseline = dfuse.get_stats()["inodes"]
                run_or_fail(f"mkdir {subdir}")
                run_or_fail(f"{helper} write {target} {size} {itr}")
                evict_and_wait(subdir, baseline)

                cmd = f"{helper} check {target} {size} {itr}"
                result = run_remote(self.log, self.hostlist_clients, cmd)
                if not result.passed:
                    failures.append(helper_failure(f"iteration {itr} size {size}", result))

        # Tail offsets let a read queued behind the pre-read arm EOF state; re-reads probe it.
        for itr in range(4):
            size = 131072
            subdir = f"{fuse_root_dir}/race.{itr}"
            target = f"{subdir}/target"
            baseline = dfuse.get_stats()["inodes"]
            run_or_fail(f"mkdir {subdir}")
            run_or_fail(f"{helper} write {target} {size} {itr}")
            evict_and_wait(subdir, baseline)

            cmd = f"{helper} race {target} {size} {itr}"
            result = run_remote(self.log, self.hostlist_clients, cmd)
            if not result.passed:
                failures.append(helper_failure(f"race {itr}", result))

        pre_read_after = dfuse.get_stats()["statistics"].get("pre_read", 0)

        # Guards non-engagement only; the stat cannot prove any check was actually drained.
        self.assertGreater(
            pre_read_after, pre_read_before, "pre-read never engaged, checks were vacuous"
        )
        if failures:
            self.fail(f"{len(failures)} corrupted reads:\n" + "\n".join(failures))

    def test_dfuse_stale_eof_after_append(self):
        """
        Test Description:
            Guard against the early-EOF cache surviving a write: read to EOF, append through
            the same handle, then re-read at the old EOF and expect the appended bytes.
            Data caching is disabled so direct-io makes every read reach dfuse.

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=vm
        :avocado: tags=dfuse
        :avocado: tags=DFusePreReadTest,test_dfuse_stale_eof_after_append
        """
        pool = self.get_pool(connect=False)
        container = self.get_container(pool)

        dfuse = get_dfuse(self, self.hostlist_clients)

        container.set_attr(attrs={"dfuse-data-cache": "off"})

        start_dfuse(self, dfuse, pool, container)

        fuse_root_dir = dfuse.mount_dir.value
        helper = os.path.join(self.prefix, "lib/daos/TESTING/tests/preread_stale_eof")

        failures = []
        for itr in range(2):
            for size in (4096, 65536):
                target = f"{fuse_root_dir}/append.{itr}.{size}"
                result = run_remote(
                    self.log, self.hostlist_clients, f"{helper} write {target} {size} {itr}"
                )
                if not result.passed:
                    self.fail(f"write of {target} failed on {result.failed_hosts}")

                result = run_remote(
                    self.log, self.hostlist_clients, f"{helper} append {target} {size} {itr}"
                )
                if not result.passed:
                    failures.append(helper_failure(f"iteration {itr} size {size}", result))

        if failures:
            self.fail(f"{len(failures)} corrupted reads after append:\n" + "\n".join(failures))
