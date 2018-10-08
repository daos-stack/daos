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
import os
import json
from pathlib       import Path

def get_file_path(bin_name):
    """
    find the binary path name in daos_m and return the list of path
    args:
        bin_name: bin file to be .
    return:
        list: list of the paths for bin_name file
    Raises:
        none
    """
    with open('../../../.build_vars.json') as json_file:
        build_paths = json.load(json_file)
    basepath = os.path.normpath(build_paths['PREFIX']  + "/../")

    return list(Path("{0}/../".format(basepath))
                .glob('**/{0}'.format(bin_name)))
