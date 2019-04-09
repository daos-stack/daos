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
import re
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

def process_host_list(hoststr):
    """
    This utility function takes a slurm style host string and returns a list
    of individual hosts.

    e.g. boro-[26-27] becomes a list with entries boro-26, boro-27

    This works for every thing that has come up so far but I don't know what
    all slurmfinds acceptable so it might not parse everything possible.
    """

    # 1st split into cluster name and range of hosts
    split_loc = hoststr.index('-')
    cluster = hoststr[0:split_loc]
    num_range = hoststr[split_loc+1:]

    # if its just a single host then nothing to do
    if num_range.isdigit():
        return [hoststr]

    # more than 1 host, remove the brackets
    host_list = []
    num_range = re.sub('\[|\]', '', num_range)

    # differentiate between ranges and single numbers
    hosts_and_ranges = num_range.split(',')
    for item in hosts_and_ranges:
        if item.isdigit():
            hostname = cluster + '-' + item
            host_list.append(hostname)
        else:
            # split the two ends of the range
            host_range = item.split('-')
            for hostnum in range(int(host_range[0]), int(host_range[1])+1):
                hostname = "{}-{}".format(cluster, hostnum)
                host_list.append(hostname)

    return host_list

class DaosTestError(Exception):
    """
    DAOS API exception class
    """
