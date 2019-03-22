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
import os
import json
from pathlib import Path
from errno import ENOENT

def get_file_path(bin_name, dir_path=""):
    """
    find the binary path name in daos_m and return the list of path
    args:
        bin_name: bin file to be.
        dir_path: Directory location on top of daos_m to find the
                  bin.
    return:
        list: list of the paths for bin_name file
    Raises:
        OSError: If failed to find the bin_name file
    """
    with open('../../../.build_vars.json') as json_file:
        build_paths = json.load(json_file)
    basepath = os.path.normpath(build_paths['PREFIX'] + "/../{0}"
                                .format(dir_path))

    file_path = list(Path(basepath).glob('**/{0}'.format(bin_name)))
    if not file_path:
        raise OSError(ENOENT, "File {0} not found inside {1} Directory"
                      .format(bin_name, basepath))
    else:
        return file_path

class DaosTestError(Exception):
    """
    DAOS API exception class
    """
