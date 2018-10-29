#!/usr/bin/python
'''
    (C) Copyright 2018 Intel Corporation.

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
import os , shutil
from  git import Repo
import subprocess
import json

class IorFailed(Exception):
    """Raise if Ior failed"""

def build_ior(basepath):
    """ Pulls the DAOS branch of IOR and builds it """

    HOME = os.path.expanduser("~")
    repo = os.path.abspath(HOME + "/ior-hpc")

    # check if there is pre-existing ior repo.
    if os.path.isdir(repo):
        shutil.rmtree(repo)

    with open(os.path.join(basepath, ".build_vars.json")) as f:
        build_paths = json.load(f)
    daos_dir = build_paths['PREFIX']

    try:
        # pulling daos branch of IOR
        Repo.clone_from("https://github.com/daos-stack/ior-hpc.git", repo, branch='daos')

        cd_cmd = 'cd ' + repo
        bootstrap_cmd = cd_cmd + ' && ./bootstrap '
        configure_cmd = cd_cmd + ' && ./configure --prefix={0} --with-daos={0}'.format(daos_dir)
        make_cmd = cd_cmd + ' &&  make install'

        # building ior
        subprocess.check_call(bootstrap_cmd, shell=True)
        subprocess.check_call(configure_cmd, shell=True)
        subprocess.check_call(make_cmd, shell=True)

    except subprocess.CalledProcessError as e:
        print "<IorBuildFailed> Exception occurred: {0}".format(str(e))
        raise IorFailed("IOR Build process Failed")

def run_ior(client_file, ior_flags, iteration, block_size, transfer_size, pool_uuid, svc_list,
            record_size, stripe_size, stripe_count, async_io, object_class, basepath, slots=1):
    """ Running Ior tests
        Function Arguments
        client_file   --client file holding client hostname and slots
        ior_flags     --all ior specific flags
        iteration     --number of iterations for ior run
        block_size    --contiguous bytes to write per task
        transfer_size --size of transfer in bytes
        pool_uuid     --Daos Pool UUID
        svc_list      --Daos Pool SVCL
        record_size   --Daos Record Size
        stripe_size   --Daos Stripe Size
        stripe_count  --Daos Stripe Count
        async_io      --Concurrent Async IOs
        object_class  --object class
        basepath      --Daos basepath
        slots         --slots on each node
    """

    with open(os.path.join(basepath, ".build_vars.json")) as f:
        build_paths = json.load(f)
    orterun_bin = os.path.join(build_paths["OMPI_PREFIX"], "bin/orterun")
    attach_info_path = basepath + "/install/tmp"
    try:

        ior_cmd = orterun_bin + " -N {0} --hostfile {1} -x DAOS_SINGLETON_CLI=1 " \
                  " -x CRT_ATTACH_INFO_PATH={2} ior {3} -i {4} -a DAOS -o `uuidgen` " \
                  " -b {5} -t {6} -- -p {7} -v {8} -r {9} -s {10} -c {11} -a {12} -o {13} "\
                  .format(slots, client_file, attach_info_path, ior_flags, iteration, block_size,
                  transfer_size, pool_uuid, svc_list, record_size, stripe_size, stripe_count,
                  async_io, object_class)
        print ("ior_cmd: {}".format(ior_cmd))

        subprocess.check_call(ior_cmd, shell=True)

    except subprocess.CalledProcessError as e:
        print "<IorRunFailed> Exception occurred: {0}".format(str(e))
        raise IorFailed("IOR Run process Failed")


# Enable this whenever needs to check
# if the script is functioning normally.
#if __name__ == "__main__":
#    IorBuild()
