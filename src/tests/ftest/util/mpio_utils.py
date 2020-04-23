#!/usr/bin/python
"""
  (C) Copyright 2019-2020 Intel Corporation.

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
  The Governments rights to use, modify, reproduce, release, perform, display,
  or disclose this software are subject to the terms of the Apache License as
  provided in Contract No. B609815.
  Any reproduction of computer software, computer software documentation, or
  portions thereof marked with this legend must also reproduce the markings.
"""
from __future__ import print_function

import os
import subprocess
from env_modules import load_mpi
from command_utils import EnvironmentVariables


class MpioFailed(Exception):
    """Raise if MPIO failed."""


class MpioUtils():
    """MpioUtils Class."""

    def __init__(self):
        """Initialize a MpioUtils object."""
        self.mpichinstall = None

    def mpich_installed(self, hostlist):
        """Check if mpich is installed.

        Args:
            hostlist (list): list of hosts

        Returns:
            bool: whether mpich is installed on the first host in the list

        """
        load_mpi('mpich')
        try:
            # checking mpich install
            command = ["/usr/bin/ssh", hostlist[0], "command -v mpichversion"]
            output = subprocess.check_output(command)
            self.mpichinstall = output.rstrip()[:-len('bin/mpichversion')]
            return True

        except subprocess.CalledProcessError as excep:
            print("Mpich not installed \n {}".format(excep))
            return False

    # pylint: disable=R0913
    def run_mpiio_tests(self, hostfile, pool_uuid, svcl, test_repo,
                        test_name, client_processes, cont_uuid):
        """Run the LLNL, MPI4PY, and HDF5 testsuites.

        Args:
            hostfile (str): client hostfile
            pool_uuid (str): pool UUID
            svcl (list): pool SVCL
            test_repo (str): test repo location
            test_name (str): name of test to be tested
            client_processes (int): number of client processes
            cont_uuid (str): container UUID

        Raises:
            MpioFailed: for an invalid test name or test execution failure

        """
        print("self.mpichinstall: {}".format(self.mpichinstall))
        # environment variables only to be set on client node
        env = EnvironmentVariables()
        env["DAOS_POOL"] = "{}".format(pool_uuid)
        env["DAOS_SVCL"] = "{}".format(":".join([str(item) for item in svcl]))
        env["DAOS_CONT"] = "{}".format(cont_uuid)
        mpirun = os.path.join(self.mpichinstall, "bin", "mpirun")

        if test_name == "romio" and os.path.isfile(
                os.path.join(test_repo, "runtests")):
            cmds = [
                [
                    "runtests",
                    env.get_export_str(),
                    os.path.join(test_repo, 'runtests'),
                    '-fname=daos:test1',
                    '-subset'
                ]
            ]

        elif test_name == "llnl" and os.path.isfile(
                os.path.join(test_repo, "testmpio_daos")):
            env["MPIO_USER_PATH"] = "daos:"
            cmds = [
                [
                    "testmpio_daos",
                    env.get_export_str(),
                    mpirun,
                    '-np',
                    str(client_processes),
                    '--hostfile',
                    hostfile,
                    os.path.join(test_repo, 'testmpio_daos'),
                    '1'
                ]
            ]

        elif test_name == "mpi4py" and os.path.isfile(
                os.path.join(test_repo, "test_io_daos.py")):
            cmds = [
                [
                    "test_io_daos.py",
                    env.get_export_str(),
                    mpirun,
                    '-np',
                    str(client_processes),
                    '--hostfile',
                    hostfile,
                    'python',
                    os.path.join(test_repo, 'test_io_daos.py')
                ]
            ]

        elif test_name == "hdf5" and (
                os.path.isfile(os.path.join(test_repo, "testphdf5")) and
                os.path.isfile(os.path.join(test_repo, "t_shapesame"))):
            env["HDF5_PARAPREFIX"] = "daos:"
            cmds = []
            for test in ["testphdf5", "t_shapesame"]:
                fqtp = os.path.join(test_repo, test)
                cmds.append(
                    [
                        fqtp,
                        env.get_export_str(),
                        mpirun,
                        '-np',
                        str(client_processes),
                        '--hostfile',
                        hostfile,
                        fqtp
                    ]
                )
        else:
            raise MpioFailed(
                "Wrong test name ({}) or test repo location ({}) "
                "specified".format(test_name, test_repo))

        for cmd in cmds:
            name = cmd.pop(0)
            print("run command: {}".format(" ".join(cmd)))
            print("***Running %s***", name)
            try:
                process = subprocess.Popen(
                    cmd, stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT)
                while True:
                    output = process.stdout.readline()
                    if output == '' and process.poll() is not None:
                        break
                    if output:
                        print(output.strip())
                if process.poll() != 0:
                    raise MpioFailed(
                        "{} Run process failed with non zero exit "
                        "code:{}".format(test_name, process.poll()))

            except (ValueError, OSError) as excep:
                raise MpioFailed(
                    "<Test FAILED> \nException occurred: {}".format(str(excep)))
