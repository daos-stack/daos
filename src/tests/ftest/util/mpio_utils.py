#!/usr/bin/python
"""
  (C) Copyright 2019-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""


import os
import sys
from command_utils_base import EnvironmentVariables, CommandFailure
from command_utils import ExecutableCommand


class MpioFailed(Exception):
    """Raise if MPIO failed."""


class MpioUtils():
    """MpioUtils Class."""

    def __init__(self):
        """Initialize a MpioUtils object."""

    # pylint: disable=R0913
    def run_mpiio_tests(self, hostlist, pool_uuid, test_repo,
                        test_name, client_processes, cont_uuid, job_manager):
        """Run the LLNL, MPI4PY, and HDF5 testsuites.

        Args:
            hostfile (str): client hostfile
            pool_uuid (str): pool UUID
            test_repo (str): test repo location
            test_name (str): name of test to be tested
            client_processes (int): number of client processes
            cont_uuid (str): container UUID
            job_manager (obj): j job manager obj

        Raises:
            MpioFailed: for an invalid test name or test execution failure

        Return:
            CmdResult: an avocado.utils.process CmdResult object containing the
                result of the command execution.

        """
        path = os.path.split(job_manager.command_path)[0]
        # fix up a relative test_repo specification
        if test_repo[0] != '/':
            test_repo = os.path.join(path, test_repo)

        # environment variables only to be set on client node
        env = EnvironmentVariables()
        env["DAOS_UNS_PREFIX"] = "daos://{}/{}/".format(pool_uuid, cont_uuid)

        executables = {
            "romio": [os.path.join(test_repo, "runtests")],
            "llnl": [os.path.join(test_repo, "testmpio_daos")],
            "mpi4py": [os.path.join(test_repo, "test_io_daos.py")],
            "hdf5": [
                os.path.join(test_repo, "testphdf5"),
                os.path.join(test_repo, "t_shapesame")
            ]
        }

        # Verify the test name is valid
        if test_name not in executables:
            raise MpioFailed(
                "Invalid test name: {} not supported".format(test_name))

        # Verify the executables exist for the valid test name
        if not all([os.path.join(exe) for exe in executables[test_name]]):
            raise MpioFailed(
                "Missing test name: {} missing executables {}".format(
                    test_name, ", ".join(executables[test_name])))

        # Setup the commands to run for this test name
        commands = []
        if test_name == "romio":
            commands.append("{} -fname=daos:/test1 -subset".format(executables[test_name][0]))
        elif test_name == "llnl":
            env["MPIO_USER_PATH"] = "daos:/"
            for exe in executables[test_name]:
                commands.append("{} 1".format(exe))
        elif test_name == "mpi4py":
            for exe in executables[test_name]:
                commands.append("python{} {}".format(sys.version_info.major, exe))
        elif test_name == "hdf5":
            env["HDF5_PARAPREFIX"] = "daos:"
            for exe in executables[test_name]:
                commands.append("{}".format(exe))

        job_manager.assign_hosts(hostlist)
        job_manager.assign_processes(client_processes)
        job_manager.assign_environment(env, True)

        for command in commands:
            print("run command: {}".format(command))
            job_manager.job = ExecutableCommand(namespace=None, command=command)
            try:
                result = job_manager.run()
            except CommandFailure as _error:
                self.fail(
                    "{} FAILED> \nException occurred: {}".format(job_manager.job, str(_error)))
        return result
