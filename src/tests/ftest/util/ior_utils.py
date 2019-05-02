#!/usr/bin/python
'''
  (C) Copyright 2018-2019 Intel Corporation.

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
from __future__ import print_function

import os
import shutil
import subprocess
import json
import sys

class IorFailed(Exception):
    """Raise if Ior failed"""

def build_ior(basepath):
    from git import Repo
    """ Pulls the DAOS branch of IOR and builds it """

    home = os.path.expanduser("~")
    repo = os.path.abspath(home + "/ior-hpc")

    # check if there is pre-existing ior repo.
    if os.path.isdir(repo):
        shutil.rmtree(repo)

    with open(os.path.join(basepath, ".build_vars.json")) as afile:
        build_paths = json.load(afile)
    daos_dir = build_paths['PREFIX']

    try:
        # pulling daos branch of IOR
        Repo.clone_from("https://github.com/daos-stack/ior-hpc.git", repo,
                        branch='daos')

        cd_cmd = 'cd ' + repo
        bootstrap_cmd = cd_cmd + ' && ./bootstrap '
        configure_cmd = (
            cd_cmd +
            ' && ./configure --prefix={0} --with-daos={0}'.format(daos_dir))
        make_cmd = cd_cmd + ' &&  make install'

        # building ior
        subprocess.check_call(bootstrap_cmd, shell=True)
        subprocess.check_call(configure_cmd, shell=True)
        subprocess.check_call(make_cmd, shell=True)

    except subprocess.CalledProcessError as error:
        print("<IorBuildFailed> Exception occurred: {0}".format(str(error)))
        raise IorFailed("IOR Build process Failed")

def get_ior_cmd(ior_flags, iteration, block_size, transfer_size, pool_uuid,
                svc_list, record_size, stripe_size, stripe_count, async_io,
                object_class, basepath, hostfile, proc_per_node=1, seg_count=1,
                filename="`uuidgen`"):
    """
    Builds an IOR command given a set of input parameters, returns the command
    as a string.  Some refactoring with run_ior will be done at a later date.
    Consider this a WIP.
    """
    # some path wrangling
    with open(os.path.join(basepath, ".build_vars.json")) as afile:
        build_paths = json.load(afile)
    orterun_bin = os.path.join(build_paths["OMPI_PREFIX"], "bin/orterun")
    attach_info_path = basepath + "/install/tmp"
    ior_bin = os.path.join(build_paths["OMPI_PREFIX"], "bin", "ior")
    lib_path = os.path.join(build_paths["OMPI_PREFIX"], "lib")

    # create the string containing the command
    ior_cmd = (
        orterun_bin + " --oversubscribe --mca mtl ^psm2,ofi "
        "-x DAOS_SINGLETON_CLI -x OFI_INTERFACE -x LD_LIBRARY_PATH "
        "-x CRT_ATTACH_INFO_PATH={1} -x CRT_PHY_ADDR_STR -x CRT_CTX_NUM "
        "-x CRT_CTX_SHARE_ADDR {16} {2} -s {3} -i {4} -a DAOS -o {5} -b {6} "
        "-t {7} --daos.pool={8} --daos.svcl={9} --daos.recordSize={10} "
        "--daos.stripeSize={11} --daos.stripeCount={12} --daos.aios={13} "
        "--daos.objectClass={14}".format(proc_per_node, attach_info_path,
                                         ior_flags, seg_count, iteration,
                                         filename, block_size, transfer_size,
                                         pool_uuid, svc_list, record_size,
                                         stripe_size, stripe_count, async_io,
                                         object_class, hostfile, ior_bin,
                                         lib_path))

    return ior_cmd


def run_ior(client_file, ior_flags, iteration, block_size, transfer_size,
            pool_uuid, svc_list, record_size, stripe_size, stripe_count,
            async_io, object_class, basepath, slots=1, seg_count=1,
            filename="`uuidgen`", display_output=True):

    """ Running Ior tests
        Function Arguments
        client_file    --client file holding client hostname and slots
        ior_flags      --all ior specific flags
        iteration      --number of iterations for ior run
        block_size     --contiguous bytes to write per task
        transfer_size  --size of transfer in bytes
        pool_uuid      --Daos Pool UUID
        svc_list       --Daos Pool SVCL
        record_size    --Daos Record Size
        stripe_size    --Daos Stripe Size
        stripe_count   --Daos Stripe Count
        async_io       --Concurrent Async IOs
        object_class   --object class
        basepath       --Daos basepath
        slots          --slots on each node
        seg_count      --segment count
        filename       --Container file name
        display_output --print IOR output on console.
    """
    with open(os.path.join(basepath, ".build_vars.json")) as afile:
        build_paths = json.load(afile)
    orterun_bin = os.path.join(build_paths["OMPI_PREFIX"], "bin/orterun")
    attach_info_path = basepath + "/install/tmp"
    try:

        ior_cmd = (
            orterun_bin + " -N {} --hostfile {} -x DAOS_SINGLETON_CLI=1 "
            "-x CRT_ATTACH_INFO_PATH={} ior {} -s {} -i {} -a DAOS -o {} -b {} "
            "-t {} -- --daos.pool {} --daos.svcl {} --daos.recordSize {} "
            "--daos.stripeSize {} --daos.stripeCount {} --daos.aios {} "
            "--daos.objectClass {} ".format(slots, client_file,
                                            attach_info_path, ior_flags,
                                            seg_count, iteration, filename,
                                            block_size, transfer_size,
                                            pool_uuid, svc_list, record_size,
                                            stripe_size, stripe_count, async_io,
                                            object_class))
        if display_output:
            print("ior_cmd: {}".format(ior_cmd))

        process = subprocess.Popen(ior_cmd, stdout=subprocess.PIPE, shell=True)
        while True:
            output = process.stdout.readline()
            if output == '' and process.poll() is not None:
                break
            if output and display_output:
                print(output.strip())
        if process.poll() != 0:
            raise IorFailed("IOR Run process Failed with non zero exit code:{}"
                            .format(process.poll()))

    except (OSError, ValueError) as error:
        print("<IorRunFailed> Exception occurred: {0}".format(str(error)))
        raise IorFailed("IOR Run process Failed")


def run_ior_mpiio(basepath, mpichinstall, pool_uuid, svcl, np, hostfile,
                  ior_flags, iteration, transfer_size, block_size,
                  display_output=True):
        """
            Running IOR over mpich
            basepath       --Daos basepath
            mpichinstall   --location of installed mpich
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
            env_variables = [
                "export CRT_ATTACH_INFO_PATH={}/install/tmp/".format(basepath),
                "export DAOS_POOL={}".format(pool_uuid),
                "export MPI_LIB=''",
                "export DAOS_SVCL={}".format(svcl),
                "export DAOS_SINGLETON_CLI=1",
                "export FI_PSM2_DISCONNECT=1"]

            run_cmd = (
                env_variables[0] + ";" + env_variables[1] + ";" +
                env_variables[2] + ";" + env_variables[3] + ";" +
                env_variables[4] + ";" + env_variables[5] + ";" +
                mpichinstall + "/mpirun -np {0} --hostfile {1} "
                "ior -a MPIIO {2} -i {3} -t {4} -b {5} "
                " -o daos:testFile".format(np, hostfile, ior_flags,
                                           iteration, transfer_size,
                                           block_size))

            if display_output:
                print ("run_cmd: {}".format(run_cmd))

            process = subprocess.Popen(run_cmd, stdout=subprocess.PIPE,
                                       shell=True)
            while True:
                output = process.stdout.readline()
                if output == '' and process.poll() is not None:
                    break
                if output and display_output:
                    print(output.strip())
            if process.poll() != 0:
                raise IorFailed("IOR Run process failed with non zero exit "
                                "code: {}".format(process.poll()))

        except (OSError, ValueError) as e:
            print("<IorRunFailed> Exception occurred: {0}".format(str(e)))
            raise IorFailed("IOR Run process Failed")

# Enable this whenever needs to check
# if the script is functioning normally.
#if __name__ == "__main__":
#    IorBuild()
