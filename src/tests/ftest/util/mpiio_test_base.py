"""
    (C) Copyright 2020-2023 Intel Corporation.

    SPDX-License-Identifier: BSD-2-Clause-Patent

"""
import os

from apricot import TestWithServers

from command_utils_base import CommandFailure, EnvironmentVariables
from job_manager_utils import get_job_manager
from mpiio_utils import LLNLCommand, Mpi4pyCommand, RomioCommand, Hdf5Command
from duns_utils import format_path


class MpiioTests(TestWithServers):
    """Run ROMIO, LLNL, MPI4PY and HDF5 test suites.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a TestWithServers object."""
        super().__init__(*args, **kwargs)
        self._test_name_class = {
            "llnl": LLNLCommand,
            "mpi4py": Mpi4pyCommand,
            "romio": RomioCommand,
            "hdf5": Hdf5Command,
        }

    def run_test(self, test_repo, test_name):
        """Execute function to be used by test functions below.

        test_repo       --absolute or relative location of test repository
        test_name       --name of the test to be run
        """
        # Select the commands to run
        if test_name not in self._test_name_class:
            self.fail("Unknown mpiio test name: {}".format(test_name))

        # initialize test specific variables
        client_processes = self.params.get("np", '/run/client_processes/')

        # Create pool
        self.add_pool(connect=False)

        # create container
        self.add_container(self.pool)

        # Pass pool and container information to the commands
        env = EnvironmentVariables()
        env["DAOS_UNS_PREFIX"] = format_path(self.pool, self.container)
        if test_name == "llnl":
            env["MPIO_USER_PATH"] = "daos:/"

        # Create commands
        kwargs_list = [{"path": test_repo}]
        if test_name == "hdf5":
            kwargs_list[0]["command"] = "testphdf5"
            kwargs_list.append(kwargs_list[0].copy())
            kwargs_list[1]["command"] = "t_shapesame"
            env["HDF5_PARAPREFIX"] = "daos:"

        self.job_manager = []
        job_managers = []
        for kwargs in kwargs_list:
            manager = get_job_manager(self)

            # fix up a relative test_repo specification
            if not kwargs["path"].startswith("/"):
                mpi_path = os.path.split(manager.command_path)[0]
                kwargs["path"] = os.path.join(mpi_path, kwargs["path"])
            if test_name == "romio":
                # Romio is not run via mpirun
                romio_job = self._test_name_class[test_name](**kwargs)
                romio_job.env = env
                job_managers.append(romio_job)
                self.job_manager[-1] = romio_job
            else:
                # finish job manager setup
                job_managers.append(manager)
                job_managers[-1].job = self._test_name_class[test_name](**kwargs)
                job_managers[-1].assign_hosts(self.hostlist_clients)
                job_managers[-1].assign_processes(client_processes)
                job_managers[-1].assign_environment(env, True)

            # Add a list of bad words that if found should fail the command
            job_managers[-1].check_results_list = [
                "non-zero exit code", "MPI_Abort", "MPI_ABORT", "ERROR"]

        for job_manager in job_managers:
            try:
                job_manager.run()
            except CommandFailure as error:
                self.fail("<{0} Test Failed> \n{1}".format(test_name, error))
