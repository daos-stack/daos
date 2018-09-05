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

class IorBuildFailed(Exception):
    "Raise if IorBuild failed"

def IorBuild(basepath):
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
        bootstrap_cmd = cd_cmd + '; ./bootstrap '
        configure_cmd = cd_cmd + '; ./configure --prefix={0} --with-daos={1}'.format(daos_dir, daos_dir)
        make_cmd = cd_cmd + ';  make install'

        # building ior
        subprocess.check_call(bootstrap_cmd, shell=True)
        subprocess.check_call(configure_cmd, shell=True)
        subprocess.check_call(make_cmd, shell=True)

    except Exception as e:
        print "<IorBuild> Exception occurred: {0}".format(str(e))
        raise IorBuildFailed("IOR Build process Failed")

# Uncomment this whenever need to check
# if the script is functioning normally.
#if __name__ == "__main__":
#    IorBuild()