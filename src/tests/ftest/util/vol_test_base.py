"""
(C) Copyright 2020-2024 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""

from apricot import TestWithServers
from command_utils import ExecutableCommand
from command_utils_base import EnvironmentVariables
from dfuse_utils import get_dfuse, start_dfuse
from exception_utils import CommandFailure


class VolTestBase(TestWithServers):
    """Runs HDF5 vol test-suites.

    :avocado: recursive
    """

    def run_test(self, job_manager, plugin_path, test_repo):
        """Run the HDF5 VOL test-suites.

        Raises:
            VolFailed: for an invalid test name or test execution failure

        """
        # initialize test specific variables
        testname = self.params.get("testname")
        client_processes = self.params.get("client_processes")

        # create pool, container and dfuse mount
        self.log_step('Creating a single pool and container')
        pool = self.get_pool(connect=False)
        container = self.get_container(pool)

        # create dfuse container
        self.log_step('Starting dfuse so VOL can run from a file system that supports xattr')
        dfuse = get_dfuse(self, self.hostlist_clients)
        start_dfuse(self, dfuse, pool, container)

        # Assign the test to run
        job_manager.job = ExecutableCommand(
            namespace=None, command=testname, path=test_repo,
            check_results=["FAILED", "stderr"])

        env = EnvironmentVariables()
        env["DAOS_POOL"] = pool.identifier
        env["DAOS_CONT"] = container.identifier
        env["HDF5_VOL_CONNECTOR"] = "daos"
        env["HDF5_PLUGIN_PATH"] = plugin_path
        env["HDF5_PREFIX"] = dfuse.mount_dir.value      # Tell the H5 suite to use this pool/cont
        env["HDF5_PARAPREFIX"] = dfuse.mount_dir.value  # Tell the H5 suite to use this pool/cont
        job_manager.assign_hosts(self.hostlist_clients)
        job_manager.assign_processes(client_processes)
        job_manager.assign_environment(env, True)

        # run VOL Command
        self.log_step(f'Running {job_manager.job.command}')
        try:
            job_manager.run()
        except CommandFailure as error:
            self.log.error(str(error))
            self.fail(f"{job_manager.job.command} failed")
