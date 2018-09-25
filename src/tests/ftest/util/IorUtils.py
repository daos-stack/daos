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

    except Exception as e:
        print "<IorBuildFailed> Exception occurred: {0}".format(str(e))
        raise IorFailed("IOR Build process Failed")

def run_ior(client_file, ior_flags, iteration, block_size, transfer_size, pool_uuid, svc_list,
            record_size, segment_count, stripe_count, async_io, object_class, basepath):
    """Running Ior tests"""

    with open(os.path.join(basepath, ".build_vars.json")) as f:
        build_paths = json.load(f)
    orterun_bin = os.path.join(build_paths["OMPI_PREFIX"], "bin/orterun")
    attach_info_path = basepath + "/install/tmp"
    try:

        ior_cmd = orterun_bin + " -N 1 --hostfile {0} -x DAOS_SINGLETON_CLI=1 " \
                  " -x CRT_ATTACH_INFO_PATH={1} ior {2} -i {3} -a DAOS -o `uuidgen` " \
                  " -b {4} -t {5} -- -p {6} -v {7} -r {8} -s {9} -c {10} -a {11} -o {12} "\
                  .format(client_file, attach_info_path, ior_flags, iteration, block_size,
                  transfer_size, pool_uuid, svc_list, record_size, segment_count, stripe_count,
                  async_io, object_class)
        print ("ior_cmd: {}".format(ior_cmd))

        subprocess.check_call(ior_cmd, shell=True)

    except Exception as e:
        print "<IorRunFailed> Exception occurred: {0}".format(str(e))
        raise IorFailed("IOR Run process Failed")


# Enable this whenever needs to check
# if the script is functioning normally.
#if __name__ == "__main__":
#    IorBuild()
