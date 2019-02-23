#!/usr/bin/python
'''
    (C) Copyright 2019 Intel Corporation.

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
    The Government's rights to use, modify, reproduce, release, perform, display,
    or disclose this software are subject to the terms of the Apache License as
    provided in Contract No. B609815.
    Any reproduction of computer software, computer software documentation, or
    portions thereof marked with this legend must also reproduce the markings.
    '''
import subprocess
import paramiko
import socket

class MpioFailed(Exception):
    """Raise if MPIO failed"""

class MpioUtils():
    """MpioUtils Class"""

    def __init__(self):

        self.mpichinstall = None

    def mpich_installed(self, hostlist):
        """Check if mpich is installed"""

        try:
            # checking mpich install
            checkmpich = subprocess.check_output(["ssh", hostlist[0], "which mpichversion"])

            # Obtaning the location where mpich is installed by removing "mpichversion"
            self.mpichinstall = checkmpich[:-13]

            return True

        except subprocess.CalledProcessError as e:
            print ("Mpich not installed \n {}".format(e))
            return False

    def run_romio(self, basepath, hostlist, romio_test_repo):
        """
            Running ROMIO testsuite under mpich
            Function Arguments:
                basepath --path where all daos and it's dependencies can be fetched from
                hostlist --list of client hosts
                romio_test_repo --built romio test directory
        """

        # environment variables only to be set on client node
        env_variables = ["export PATH={}/bin:$PATH".format(self.mpichinstall),
                         "export LD_LIBRARY_PATH={}/lib:$LD_LIBRARY_PATH".format(self.mpichinstall),
                         "export CRT_ATTACH_INFO_PATH={}/install/tmp/".format(basepath),
                         "export MPI_LIB=''", "export DAOS_SINGLETON_CLI=1"]

        # setting attributes
        cd_cmd = 'cd ' + romio_test_repo
        test_cmd = './runtests -fname=daos:test1 -subset -daos'
        run_cmd = cd_cmd + ' && ' + test_cmd

        try:
            # establish conection and run romio test
            # All the tests must pass with "No Errors"
            # if any test fails it should return "non-zero exit code"
            ssh = paramiko.SSHClient()
            ssh.load_system_host_keys()
            ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
            ssh.connect(hostlist[0])
            ssh_stdin, ssh_stdout, ssh_stderr = ssh.exec_command(env_variables[0] + \
                                   " && " + env_variables[1] + " && " + env_variables[2] + \
                                   " && " + env_variables[3] + " && " + env_variables[4] + \
                                   " && " + run_cmd)
            print ssh_stdout.read()
            print ssh_stderr.read()

        except (IOError, OSError, paramiko.SSHException, socket.error) as e:
            raise MpioFailed("<ROMIO Test FAILED> \nException occurred: {}".format(str(e)))

    def run_ior(self, basepath, pool_uuid, svcl, np, hostfile, ior_flags, iteration,
                transfer_size, block_size, display_output=True):
        """
            Running IOR over mpich
            basepath       --Daos basepath
            pool_uuid      --Daos Pool UUID
            svcl           --Daos Pool SVCL
            np             --number of client processes
            hostfile       --client file holding client hostname and slots
            ior_flags      --all ior specific flags
            iteration      --number of iterations for ior run
            block_size     --contiguous bytes to write per task
            transfer_size  --size of transfer in bytes
            display_output --print IOR output on console.
        """
        try:
            # environment variables only to be set on client node
            env_variables = ["export CRT_ATTACH_INFO_PATH={}/install/tmp/".format(basepath),
                             "export DAOS_POOL={}".format(pool_uuid), "export MPI_LIB=''",
                             "export DAOS_SVCL={}".format(svcl), "export DAOS_SINGLETON_CLI=1",
                             "export FI_PSM2_DISCONNECT=1"]

            run_cmd = env_variables[0] + ";" + env_variables[1] + ";" + env_variables[2] + \
                      ";" + env_variables[3] + ";" + env_variables[4] + ";" + env_variables[5] + \
                      ";" + self.mpichinstall + "/mpirun -np {0} --hostfile {1} \
                      /home/standan/mpiio/ior/build/src/ior -a MPIIO {2} -i {3} -t {4} -b {5} \
                      -o daos:testFile".format(np, hostfile, ior_flags, iteration,
                                               transfer_size, block_size)

            if display_output:
                print ("run_cmd: {}".format(run_cmd))

            process = subprocess.Popen(run_cmd, stdout=subprocess.PIPE, shell=True)
            while True:
                output = process.stdout.readline()
                if output == '' and process.poll() is not None:
                    break
                if output and display_output:
                    print output.strip()
            if process.poll() != 0:
                raise MpioFailed("IOR Run process Failed with non zero exit code:{}"
                                 .format(process.poll()))

        except (OSError, ValueError) as e:
            print "<IorRunFailed> Exception occurred: {0}".format(str(e))
            raise MpioFailed("IOR Run process Failed")
