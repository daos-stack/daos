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
from command_utils_base import EnvironmentVariables


class MpioFailed(Exception):
    """Raise if MPIO failed."""


class MpioUtils():
    """MpioUtils Class."""

    def __init__(self):
        """Initialize a MpioUtils object."""
        self.mpichinstall = None

    def mpich_installed(self, hostlist):
        """Check if mpich is installed."""
        load_mpi('mpich')

        try:
            # checking mpich install
            self.mpichinstall = subprocess.check_output(
                ["ssh", hostlist[0],
                 "command -v mpichversion"]).rstrip()[:-len('bin/mpichversion')]

            return True

        except subprocess.CalledProcessError as excep:
            print("Mpich not installed \n {}".format(excep))
            return False

    # pylint: disable=R0913
    def run_mpiio_tests(self, hostfile, pool_uuid, svcl, test_repo,
                        test_name, client_processes, cont_uuid):
        """Run LLNL, MPI4PY and HDF5 testsuites.

        Args:
            hostfile (str): client hostfile
            pool_uuid (str): pool UUID
            svcl (list): pool SVCL
            test_repo (str): test repo location
            test_name (str): name of test to be tested
            client_processes (int): number of client processes per host
            cont_uuid (str): container UUID

        Raises:
            MpioFailed: if the test name is invalid or there is an error running
                the valid test name successfully

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
            test_cmd = [env.get_export_str(),
                        os.path.join(test_repo, 'runtests'),
                        '-fname=daos:test1',
                        '-subset']
            cmd = " ".join(test_cmd)
        elif test_name == "llnl" and os.path.isfile(
                os.path.join(test_repo, "testmpio_daos")):
            env["MPIO_USER_PATH"] = "daos:"
            test_cmd = [env.get_export_str(),
                        mpirun,
                        '-np',
                        str(client_processes),
                        '--hostfile',
                        hostfile,
                        os.path.join(test_repo, 'testmpio_daos'),
                        '1']
            cmd = " ".join(test_cmd)
        elif test_name == "mpi4py" and os.path.isfile(
                os.path.join(test_repo, "test_io_daos.py")):
            test_cmd = [env.get_export_str(),
                        mpirun,
                        '-np',
                        str(client_processes),
                        '--hostfile',
                        hostfile,
                        'python',
                        os.path.join(test_repo, 'test_io_daos.py')]
            cmd = " ".join(test_cmd)
        elif test_name == "hdf5" and (
                os.path.isfile(os.path.join(test_repo, "testphdf5")) and
                os.path.isfile(os.path.join(test_repo, "t_shapesame"))):
            env["HDF5_PARAPREFIX"] = "daos:"
            cmd = ''
            for test in ["testphdf5", "t_shapesame"]:
                fqtp = os.path.join(test_repo, test)
                test_cmd = [env.get_export_str(),
                            'echo ***Running {}*** ;'.format(fqtp),
                            mpirun,
                            '-np',
                            str(client_processes),
                            '--hostfile',
                            hostfile,
                            fqtp + ';']
                cmd += " ".join(test_cmd)
        else:
            raise MpioFailed("Wrong test name ({}) or test repo location ({}) "
                             "specified".format(test_name, test_repo))

        print("run command: {}".format(cmd))

        try:
            process = subprocess.Popen(cmd, stdout=subprocess.PIPE,
                                       stderr=subprocess.STDOUT, shell=True)
            while True:
                output = process.stdout.readline()
                if output == '' and process.poll() is not None:
                    break
                if output:
                    print(output.strip())
            if process.poll() != 0:
                raise MpioFailed("{} Run process".format(test_name)
                                 + " Failed with non zero exit"
                                 + " code:{}".format(process.poll()))

        except (ValueError, OSError) as excep:
            raise MpioFailed(
                "<Test FAILED> \nException occurred: {}".format(str(excep)))
