"""
  (C) Copyright 2026 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os

from apricot import TestWithServers
from command_utils_base import EnvironmentVariables
from dfuse_utils import get_dfuse, start_dfuse
from run_utils import run_remote

# Marker printed to stderr by libpil4dfs at process exit when D_IL_REPORT is set
# and interception is enabled. Its presence/absence tells us whether interception
# was active for the process.
INTERCEPT_MARKER = "libpil4dfs intercepting summary"


class Pil4dfsManyMounts(TestWithServers):
    """Verify libpil4dfs handling of many dfuse mount points (MAX_DAOS_MT).

    libpil4dfs discovers every fuse.daos mount point listed in /proc/self/mounts
    when it initializes and stores them in a fixed-size table (MAX_DAOS_MT). When
    the number of mount points is at or below the limit, interception is enabled
    and used for all of them. When the number exceeds the limit, libpil4dfs must
    gracefully disable interception (falling back to dfuse) rather than aborting
    the application, so that no core file is produced.

    :avocado: recursive
    """

    def _run_case(self, pool, env_str, mount_count, expect_intercept):
        """Mount mount_count dfuse instances and run a single libpil4dfs process across them.

        Args:
            pool (TestPool): pool to create the containers in.
            env_str (str): shell prefix that loads libpil4dfs and enables D_IL_REPORT.
            mount_count (int): number of dfuse mount points to mount simultaneously.
            expect_intercept (bool): whether interception is expected to be enabled.
        """
        self.log_step(
            f"Case: {mount_count} mount points, "
            f"expecting interception to be {'enabled' if expect_intercept else 'disabled'}")

        dfuses = []
        mount_dirs = []
        try:
            for _ in range(mount_count):
                container = self.get_container(pool)
                dfuse = get_dfuse(self, self.hostlist_clients)
                start_dfuse(self, dfuse, pool, container)
                dfuses.append(dfuse)
                mount_dirs.append(dfuse.mount_dir.value)

            # A single libpil4dfs-intercepted process that touches every mount point. At
            # initialization libpil4dfs discovers all fuse.daos mounts in /proc/self/mounts,
            # so this exercises the MAX_DAOS_MT table regardless of which mount is accessed.
            stat_cmd = env_str + "stat " + " ".join(mount_dirs)
            result = run_remote(self.log, self.hostlist_clients, stat_cmd)

            # The process must always complete cleanly, regardless of how many mounts are
            # present. Over the limit, libpil4dfs must disable interception gracefully and
            # never abort (which would create a core file and fail the CI stage).
            if not result.passed:
                self.fail(
                    f"libpil4dfs process failed with {mount_count} mount points on "
                    f"{result.failed_hosts}; it must never abort")

            intercepted = INTERCEPT_MARKER in result.joined_stdout

            # Log the observed interception status so the test log shows each case behaving
            # as expected (interception enabled at/below MAX_DAOS_MT, disabled above it).
            self.log.info(
                "Case result: %d mount points -> process succeeded, interception %s "
                "(expected %s)", mount_count, "enabled" if intercepted else "disabled",
                "enabled" if expect_intercept else "disabled")

            if expect_intercept and not intercepted:
                self.fail(
                    f"Expected interception to be enabled with {mount_count} mount points, "
                    "but the libpil4dfs summary was not found")
            if not expect_intercept and intercepted:
                self.fail(
                    f"Expected interception to be disabled with {mount_count} mount points "
                    "(more than MAX_DAOS_MT), but the libpil4dfs summary was found")
        finally:
            # Unmount this case's dfuse instances so the next case only sees its own mounts.
            for dfuse in dfuses:
                dfuse.stop()

    def test_pil4dfs_many_mounts(self):
        """JIRA ID: DAOS-18890.

        Test Description:
            Verify libpil4dfs behavior with dfuse mount point counts at/below and
            above MAX_DAOS_MT, all within a single test run. No case may produce a
            core file.

            Steps:
                1.) Create a single pool.
                2.) For each count in intercept_mount_counts, mount that many dfuse
                    instances and confirm a single libpil4dfs process uses them all
                    (interception enabled).
                3.) Mount no_intercept_mount_count dfuse instances (more than
                    MAX_DAOS_MT) and confirm the libpil4dfs process completes without
                    aborting and with interception disabled.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=dfuse,pil4dfs
        :avocado: tags=Pil4dfsManyMounts,test_pil4dfs_many_mounts
        """
        intercept_mount_counts = self.params.get(
            "intercept_mount_counts", "/run/test/*", [10, 32])
        no_intercept_mount_count = self.params.get(
            "no_intercept_mount_count", "/run/test/*", 33)

        lib_path = os.path.join(self.prefix, "lib64", "libpil4dfs.so")
        env_str = EnvironmentVariables({
            "LD_PRELOAD": lib_path,
            "D_IL_NO_BYPASS": 1,
            "D_IL_REPORT": 1
        }).to_export_str()

        self.log_step("Creating a single pool")
        pool = self.get_pool(connect=False)

        for mount_count in intercept_mount_counts:
            self._run_case(pool, env_str, mount_count, expect_intercept=True)

        self._run_case(
            pool, env_str, no_intercept_mount_count, expect_intercept=False)

        self.log.info("Test passed")
